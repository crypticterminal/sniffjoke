#include "sniffjoke.h"
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <csignal>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/un.h>

/* process configuration, data struct defined in sniffjoke.h */
static struct sj_useropt useropt;

/* Sniffjoke networking and feature configuration */
static SjConf *sjconf;
/* Sniffjoke man in the middle class and functions */
static NetIO *mitm;
/* process tracking, handling, killing, breeding, ecc... */
static Process *SjProc;

#define SNIFFJOKE_HELP_FORMAT \
	"%s [command] or %s --options:\n"\
	" --debug [level 1-4]\tenable debug and set the verbosity [default:1]\n"\
	" --logfile [file]\tset a logfile, [default %s]\n"\
	" --user [username]\tdowngrade priviledge to the specified user [default:nobody]\n"\
	" --group [groupname]\tdowngrade priviledge to the specified group [default:users]\n"\
	" --chroot-dir [dir]\truns chroted into the specified dir [default:disabled]\n"\
	" --force\t\tforce restart if sniffjoke service\n"\
	" --foreground\t\trunning in foreground\n"\
	" --version\t\tshow sniffjoke version\n"\
	" --help\t\t\tshow this help\n\n"\
	"while sniffjoke is running, you should send one of those commands as command line argument:\n"\
	" start\t\t\tstart sniffjoke hijacking/injection\n"\
	" stop\t\t\tstop sniffjoke (but remain tunnel interface active)\n"\
	" quit\t\t\tstop sniffjoke, save config, abort the service\n"\
	" stat\t\t\tget statistics about sniffjoke configuration and network\n\n"\
	" set start end value\tset per tcp ports the strongness of injection\n"\
	" \t\t\tthe values are: [heavy|normal|light|none]\n"\
	" \t\t\texample: sniffjoke set 22 80 heavy\n"\
	" clear\t\t\talias to \"set 1 65535 none\"\n"\
	" showport\t\tshow TCP ports strongness of injection\n"\
	" loglevel\t\t0 = normal, 1 = verbose, 2 = debug\n\n"\
	"\t\t\thttp://www.delirandom.net/sniffjoke\n"

#define SNIFFJOKE_HACKING_HELP \
	" the option --hacking [value] enable or disable some hack, is used with a test script\n"\
	" usage: --hacking 0123456789AB (12 positions: \"Y\" enable, \"N\" disable) 12 hacks:\n\n"\
	"  1] fake data (default: YES)\n"\
	"  2] fake seq (default: YES - need check)\n"\
	"  3] fake close (default: YES - verify FIN/RST diffs)\n"\
	"  4] fake zero window (default: YES)\n"\
	"  5] valid rst fake seq (default: YES)\n"\
	"  6] fake syn (default: NO - cause a troblue ?)\n"\
	"  7] shift ack (default: NO - need testing, cause ack storm, cwnd downgrade)\n"\
	"  8] half fake syn (default: NO - not implemented)\n"\
	"  9] half fake ack (default: NO - not implemented)\n"\
	" 10] inject IPOPT (default: YES - need a lot of research)\n"\
	" 11] inject TCPOPT (default: YES - need research too)\n"\
	" 12] fake data (ant|post)icipation (default: YES)\n\n"\
	" example: --hacking YNNNYYYNYNYN (7 and 8 position: IGNORED\n"

static void sj_hacking_help(void) {
	printf(SNIFFJOKE_HACKING_HELP);
}

static void sj_help(const char *pname) {
	printf(SNIFFJOKE_HELP_FORMAT, pname, pname, LOGFILE, "127.0.0.1");
}

static void sj_version(const char *pname) {
	printf("%s %s\n", SW_NAME, SW_VERSION);
}

static void sj_forced_clean_exit(pid_t pid) {
	/* the function return when the process is dead */  
	bool dead = false;
	int killret;
	internal_log(NULL, ALL_LEVEL, "killing process (%d)", pid);

	/* kill! */
	killret = kill(pid, SIGTERM);
	
	while (!dead) {
		if (!killret)
			usleep(50000);
		else if (errno == EPERM) {
			internal_log(NULL, ALL_LEVEL, "you have not privileges to kill process (%d)", pid);
			exit(0);
		} else /* (errno == ESRCH) */ {
			dead = true;
			internal_log(NULL, ALL_LEVEL, "forced quit of previous process: (%d) is dead", pid);
		}
		
		/* test if the pid is running again */
		killret = kill(pid, SIGUSR1);
	}
}

static void sj_sigtrap(int signal) 
{
	if (signal)
		internal_log(NULL, ALL_LEVEL, "received signal %d, cleaning sniffjoke objects...", signal);

	delete mitm;
	delete sjconf;

	SjProc->CleanExit();

	raise(SIGKILL);
}

/* internal routine called in client_send_command and sj_srv_child_check_local_unixserv */
static int 
service_listener(int sock, char *databuf, int bufsize, struct sockaddr *from, FILE *error_flow, const char *usermsg) {

	memset(databuf, 0x00, bufsize);

	/* we receive up to bufsize -1 having databuf[bufsize] = 0 and saving us from future segfaults */

	int fromlen = sizeof(struct sockaddr_un), ret;

	if ((ret = recvfrom(sock, databuf, bufsize, 0, from, (socklen_t *)&fromlen)) == -1) 
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return 0;
		}

		internal_log(error_flow, ALL_LEVEL, "unable to receive local socket: %s: %s", usermsg, strerror(errno));
	}

	return ret;
}

static int sj_bind_unixsocket() 
{
	const char *sniffjoke_socket_path = SJ_SERVICE_UNIXSOCK; 
	struct sockaddr_un sjsrv;
	int sock;

	if ((sock = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		internal_log(NULL, ALL_LEVEL, "FATAL ERROR: : unable to open unix socket: %s", strerror(errno));
		SjProc->CleanExit(true);
	}

	memset(&sjsrv, 0x00, sizeof(sjsrv));
	sjsrv.sun_family = AF_UNIX;
	memcpy(sjsrv.sun_path, sniffjoke_socket_path, strlen(sniffjoke_socket_path));

	if (!access(sniffjoke_socket_path, F_OK)) {
		if (unlink(sniffjoke_socket_path)) {
			internal_log(NULL, ALL_LEVEL, "FATAL ERROR: unable to unlink previous instance of %s: %s", 
			sniffjoke_socket_path, strerror(errno));
			SjProc->CleanExit(true);
		}
	}
								
	if (bind(sock, (struct sockaddr *)&sjsrv, sizeof(sjsrv)) == -1) {
		close(sock);
		internal_log(NULL, ALL_LEVEL, "FATAL ERROR: unable to bind unix socket %s: %s", 
					 sniffjoke_socket_path, strerror(errno)
		);
		SjProc->CleanExit(true);
	}

	if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
		close(sock);
		internal_log(NULL, ALL_LEVEL, "FATAL ERROR: unable to set non blocking unix socket %s: %s",
					sniffjoke_socket_path, strerror(errno)
		);

		SjProc->CleanExit(true);
	}
	internal_log(NULL, VERBOSE_LEVEL, "opened unix socket %s", sniffjoke_socket_path);
	
	return sock;
}

/* function used in in order to receive command and modify the running conf, display stats and so on */
static void sj_srv_child_check_local_unixserv(int srvsock, SjConf *confobj) {

	char received_command[MEDIUMBUF], *output =NULL, *internal_buf =NULL;
	int i, rlen, cmdlen;
	struct sockaddr_un fromaddr;

	if ((rlen = service_listener(srvsock, received_command, MEDIUMBUF, (struct sockaddr *)&fromaddr, NULL, "from the command receiving engine")) == -1) 
		SjProc->CleanExit(true);

	if (!rlen)
		return;

	internal_log(NULL, VERBOSE_LEVEL, "received command from the client: %s", received_command);

	if (!memcmp(received_command, "stat", strlen("stat"))) {
		output = sjconf->handle_cmd_stat();
	} else if (!memcmp(received_command, "start", strlen("start"))) {
		output = sjconf->handle_cmd_start();
	} else if (!memcmp(received_command, "stop", strlen("stop"))) {
		output = sjconf->handle_cmd_stop();
	} else if (!memcmp(received_command, "quit", strlen("quit"))) {
		output = sjconf->handle_cmd_quit();
	} else if (!memcmp(received_command, "set", strlen("set"))) {
		int start_port, end_port, value;

		/* TODO - support ports rangeSTART-rangeEND or port,another,again,sookiestackhouse,billcarson,65535 */
		/* FIXME - at the moment only integer value are accepted: 0 1 2 3 4 */
		sscanf(received_command, "set %d %d %d", &start_port, &end_port, &value);

		if (start_port < 0 || start_port > PORTNUMBER || end_port < 0 || end_port > PORTNUMBER || 
			value < 0 || value >= 0x05) 
		{
			internal_buf = (char *)malloc(MEDIUMBUF);
			snprintf(internal_buf, MEDIUMBUF, "invalid port, %d or %d, must be > 0 and < %d",
				start_port, end_port, PORTNUMBER);
			internal_log(NULL, ALL_LEVEL, "%s", internal_buf);
			output = internal_buf;
		}
		else {
			output = sjconf->handle_cmd_set(start_port, end_port, value);
		}
	} else if (!memcmp(received_command, "clear", strlen("clear"))) {
		output = sjconf->handle_cmd_set(1, PORTNUMBER, NONE);
	} else if (!memcmp(received_command, "showport", strlen("showport"))) {
		output = sjconf->handle_cmd_showport();
	} else if (!memcmp(received_command, "loglevel", strlen("loglevel")))  {
		int loglevel;

		sscanf(received_command, "loglevel %d", &loglevel);
		if (loglevel < 0 || loglevel > HACKS_DEBUG) {
			internal_buf = (char *)malloc(MEDIUMBUF);
			printf("%d\n", loglevel);
			snprintf(internal_buf, MEDIUMBUF, "invalid log value: %d, must be > 0 and < than %d", loglevel, HACKS_DEBUG);
			internal_log(NULL, ALL_LEVEL, "%s", internal_buf);
			output = internal_buf;
		} else {
			output = sjconf->handle_cmd_log(loglevel);
		}
	} else {
		internal_log(NULL, ALL_LEVEL, "wrong command %s", received_command);
	}

	/* send the answer message to the client */
	if (output != NULL)
		sendto(srvsock, output, strlen(output), 0, (struct sockaddr *)&fromaddr, sizeof(fromaddr));

	if (internal_buf != NULL)
		free(internal_buf);
}

static void client_send_command(char *cmdstring)
{
	int sock;
	char received_buf[HUGEBUF];
	struct sockaddr_un servaddr;/* address of server */
	struct sockaddr_un clntaddr;/* address of client */
	struct sockaddr_un from; /* address used for receiving data */
	int rlen;
	
	/* Create a UNIX datagram socket for client */
	if ((sock = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		internal_log(NULL, ALL_LEVEL, "FATAL ERROR: unable to open UNIX socket for connect to sniffjoke service: %s", strerror(errno));
		exit(0);
	}
		
	/* Client will bind to an address so the server/service will get an address in its recvfrom call and use it to
	 * send data back to the client.  
	 */
	memset(&clntaddr, 0x00, sizeof(clntaddr));
	clntaddr.sun_family = AF_UNIX;
	strcpy(clntaddr.sun_path, SJ_CLIENT_UNIXSOCK);

	unlink(SJ_CLIENT_UNIXSOCK);
	if (bind(sock, (const sockaddr *)&clntaddr, sizeof(clntaddr)) == -1) {
		internal_log(NULL, ALL_LEVEL, "FATAL ERROR: unable to bind client to %s: %s", SJ_CLIENT_UNIXSOCK, strerror(errno));
		exit(0);
	}

	/* Set up address structure for server/service socket */
	memset(&servaddr, 0x00, sizeof(servaddr));
	servaddr.sun_family = AF_UNIX;
	strcpy(servaddr.sun_path, SJ_SERVICE_UNIXSOCK);

	if (sendto(sock, cmdstring, strlen(cmdstring), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
		internal_log(NULL, ALL_LEVEL, "FATAL ERROR: unable to send message [%s]: %s", cmdstring, strerror(errno));
		exit(0);
	}

	/* We receive a max of HUGEBUF -1 saving us from segfault during printf */
	if ((rlen = service_listener(sock, received_buf, HUGEBUF, (struct sockaddr *)&from, stdout, "from the command sending engine")) == -1) 
		exit(0); // the error message has been delivered 

	if (rlen == 0)
		internal_log(NULL, ALL_LEVEL, "unreceived response for the command [%s]", cmdstring);
	else	/* the output */ 
		printf("<sniffjoke service>: %s", received_buf);
	
	close(sock);
}

void check_call_ret(const char *umsg, int objerrno, int ret, bool fatal) {
		char errbuf[STRERRLEN];
		int my_ret = 0;

		internal_log(NULL, DEBUG_LEVEL, "checking errno %d message of [%s], return value: %d fatal %d", objerrno, umsg, ret, fatal);

		if (ret != -1)
				return;

		if (objerrno)
				snprintf(errbuf, STRERRLEN, "%s: %s", umsg, strerror(objerrno));
		else
				snprintf(errbuf, STRERRLEN, "%s ", umsg);

		if (fatal) {
				internal_log(NULL, ALL_LEVEL, "fatal error: %s", errbuf);
				SjProc->CleanExit(true);
		} else {
				internal_log(NULL, ALL_LEVEL, "error: %s", errbuf);
		}
}

/* forceflow is almost useless, use NULL in the normal logging options */
void internal_log(FILE *forceflow, int errorlevel, const char *msg, ...) 
{
	va_list arguments;
	time_t now = time(NULL);
	FILE *output_flow;

	if (forceflow == NULL && useropt.logstream == NULL)
		return;

	if (forceflow != NULL)
		output_flow = forceflow;
	else
		output_flow = useropt.logstream;

	if (errorlevel == PACKETS_DEBUG && useropt.packet_logstream != NULL)
		output_flow = useropt.packet_logstream;

	if (errorlevel == HACKS_DEBUG && useropt.hacks_logstream != NULL)
		output_flow = useropt.hacks_logstream;

	if (errorlevel <= useropt.debug_level) {
		char *time = strdup(asctime(localtime(&now)));

		va_start(arguments, msg);
		time[strlen(time) -1] = ' ';
		fprintf(output_flow, "%s ", time);
		vfprintf(output_flow, msg, arguments);
		fprintf(output_flow, "\n");
		fflush(output_flow);
		va_end(arguments);
		free(time);
	}
}

int main(int argc, char **argv) {
	int i, charopt, local_input = 0;
	bool restart_on_restore = false;
	char command_buffer[MEDIUMBUF], *command_input = NULL;
	bool srv_just_active = 0;
	int listening_unix_socket;
	
	/* set the default values in the configuration struct */
	useropt.cfgfname = CONF_FILE;
	useropt.user = DROP_USER;
	useropt.group = DROP_GROUP;
	useropt.chroot_dir = CHROOT_DIR;
	useropt.logfname = LOGFILE;
	useropt.debug_level = DEFAULT_DEBUG_LEVEL;
	useropt.cfgfname = CONF_FILE;

	useropt.go_foreground = false;
	useropt.force_restart = false;
	useropt.logstream = stdout;
	useropt.packet_logstream = stdout;
	useropt.hacks_logstream = stdout;
	
	struct option sj_option[] =
	{
		{ "conf", required_argument, NULL, 'f' },
		{ "user", required_argument, NULL, 'u' },
		{ "group", required_argument, NULL, 'g' },
		{ "chroot-dir", required_argument, NULL, 'c' },
		{ "debug", required_argument, NULL, 'd' },
		{ "logfile", required_argument, NULL, 'l' },
		{ "foreground", optional_argument, NULL, 'x' },
		{ "force", optional_argument, NULL, 'r' },
		{ "version", optional_argument, NULL, 'v' },
		{ "help", optional_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	memset(command_buffer, 0x00, MEDIUMBUF);
	/* check for direct commands */
	if ((argc >= 2) && !memcmp(argv[1], "start", strlen("start"))) {
		snprintf(command_buffer, MEDIUMBUF, "start");
		command_input = argv[1];
	}
	if ((argc >= 2) && !memcmp(argv[1], "stop", strlen("stop"))) {
		snprintf(command_buffer, MEDIUMBUF, "stop");
		command_input = argv[1];
	}
	if ((argc >= 2) && !memcmp(argv[1], "stat", strlen("stat"))) {
		snprintf(command_buffer, MEDIUMBUF, "stat");
		command_input = argv[1];
	}
	if ((argc == 5) && !memcmp(argv[1], "set", strlen("set"))) {
		snprintf(command_buffer, MEDIUMBUF, "set %s %s %s", argv[2], argv[3], argv[4]);
		command_input = command_buffer;
	} 
	if ((argc == 2) && !memcmp(argv[1], "clear", strlen("clear"))) {
		snprintf(command_buffer, MEDIUMBUF, "clear");
		command_input = command_buffer;
	} 
	if ((argc == 2) && !memcmp(argv[1], "showport", strlen("showport"))) {
		snprintf(command_buffer, MEDIUMBUF, "showport");
		command_input = command_buffer;
	} 
	if ((argc == 2) && !memcmp(argv[1], "quit", strlen("quit"))) {
		snprintf(command_buffer, MEDIUMBUF, "quit");
		command_input = command_buffer;
	}
	if ((argc == 3) && !memcmp(argv[1], "loglevel", strlen("loglevel"))) {
		snprintf(command_buffer, MEDIUMBUF, "loglevel %s", argv[2]);
		command_input = command_buffer;
	}

	SjProc = new Process(&useropt);
	if (SjProc->failure) {
		sj_help(argv[0]);
		delete SjProc;
		return 0;
	}

	if (command_input == NULL) {
		while ((charopt = getopt_long(argc, argv, "f:u:g:c:l:d:xrvh", sj_option, NULL)) != -1) {
			switch(charopt) {
				case 'f':
					useropt.cfgfname = strdup(optarg);
					break;
				case 'u':
					useropt.user = strdup(optarg);
					break;
				case 'g':
					useropt.group = strdup(optarg);
					break;
				case 'c':
					useropt.chroot_dir = strdup(optarg);
					break;
				case 'l':
					useropt.logfname = strdup(optarg);
					break;
				case 'd':
					useropt.debug_level = atoi(optarg);
					break;
				case 'x':
					useropt.go_foreground = true;
					break;
				case 'r':
					useropt.force_restart = true;
					break;
				case 'v':
					sj_version(argv[0]);
					return 0;
				case 'h':
				default:
					sj_help(argv[0]);
					return -1;

				argc -= optind;
				argv += optind;
			}
		}
	
	}

	/* importing configuration: contains both service and client user options */
	sjconf = new SjConf(&useropt);

	/* client-like usage: if a command line is present, send the command to the running sniffjoke service */
	if (command_input != NULL) 
	{
		if (SjProc->isServiceRunning() == false) {
			internal_log(NULL, ALL_LEVEL, "warning: sniffjoke is not running, command  %s ignored", command_input);
			return 0;
		}
		
		if (SjProc->isClientRunning() == true) {
			internal_log(NULL, ALL_LEVEL, "an other client is active, exiting");
			return 0;
		}

		SjProc->SetProcType(Process::SJ_PROCESS_CLIENT);
		SjProc->setLocktoExist(SJ_CLIENT_LOCK);

		/* if is not specify an user/group, nobody/nogroup are sets */

		SjProc->Jail(useropt.chroot_dir, sjconf->running);
		if (SjProc->failure == true) {
			internal_log(NULL, ALL_LEVEL, "error in process handling, closing");
			delete SjProc;
		}

		SjProc->PrivilegesDowngrade(sjconf->running);

		client_send_command(command_input);

#if 0
		/* if the command sent was "quit", we need to kill the father. should not be
		 * self killed with raise(), and so we need another string check */
		if ((argc == 2) && !memcmp(argv[1], "quit", strlen("quit"))) {
			int sj_srv_father_pid = SjProc->readPidfile(SJ_SERVICE_FATHER_PID_FILE);
			kill(sj_srv_father_pid, SIGTERM);
			printf("mah ?\n");
		}
#endif

		SjProc->ReleaseLock(SJ_CLIENT_LOCK);
		SjProc->CleanExit();
	}

	if (argc > 1 && argv[1][0] != '-') {
		internal_log(stderr, ALL_LEVEL, "wrong usage of sniffjoke: beside commands, only --long-opt are accepted");
		sj_help(argv[0]);
		return -1;
	}

	if (SjProc->isServiceRunning()) 
	{
		/* da pulire questo pezzo, togliere tutti i pidfile quando si appura che non servono piu' */
		int sj_srv_father_pid = SjProc->readPidfile(SJ_SERVICE_FATHER_PID_FILE);
		int sj_srv_child_pid = SjProc->readPidfile(SJ_SERVICE_CHILD_PID_FILE);

		if ((sj_srv_father_pid || sj_srv_child_pid) && (!useropt.force_restart)) {
			internal_log(stderr, ALL_LEVEL, "sniffjoke is already running, use --force or check --help");
			return 0;
		}

		if (sj_srv_father_pid) {
			sj_forced_clean_exit(sj_srv_father_pid); 
			internal_log(NULL, VERBOSE_LEVEL, "forced kill of service father %d process...", sj_srv_father_pid);
		}

		if (sj_srv_child_pid) {
			sj_forced_clean_exit(sj_srv_child_pid); 
			internal_log(NULL, VERBOSE_LEVEL, "forced kill of service child %d process...", sj_srv_child_pid);
		}
	} 

	SjProc->setLocktoExist(SJ_SERVICE_LOCK);

	if (!useropt.go_foreground) 
	{
		SjProc->SjBackground();
		SjProc->processIsolation();

		if (SjProc->failure) 
			delete SjProc;
	}
	else {
		useropt.logstream = stdout;
		internal_log(NULL, ALL_LEVEL, "foreground running: logging set on standard output, block with ^c");
	}


	/* checking config file */
	if (useropt.cfgfname != NULL && access(useropt.cfgfname, W_OK)) {
		internal_log(NULL, ALL_LEVEL, "unable to access %s: sniffjoke will use the defaults", useropt.cfgfname);
	}
	
	/* setting ^C, SIGTERM and other signal trapped for clean network environment */
	SjProc->sigtrapSetup(sj_sigtrap);

	/* the code flow reach here, SniffJoke is ready to instance network environment */
	mitm = new NetIO(sjconf);

	if (mitm->is_network_down())
	{
		internal_log(stderr, ALL_LEVEL, "detected network error in NetIO constructor: unable to start sniffjoke");
		SjProc->CleanExit(true);
	}

	SjProc->processDetach();
	SjProc->Jail(useropt.chroot_dir, sjconf->running);

	if (!useropt.go_foreground) {	
		if ((useropt.logstream = fopen(useropt.logfname, "a+")) == NULL) {
			internal_log(stderr, ALL_LEVEL, "FATAL ERROR: unable to open %s: %s", useropt.logfname, strerror(errno));
			SjProc->CleanExit(true);
		}
		else
			internal_log(stderr, DEBUG_LEVEL, "opened log file %s", useropt.logfname);
			
		if (useropt.debug_level >= PACKETS_DEBUG) {
			char *tmpfname = (char *)malloc(strlen(useropt.logfname) + 10);
			sprintf(tmpfname, "%s.packets", useropt.logfname);
			if ((useropt.packet_logstream = fopen(tmpfname, "a+")) == NULL) {
				internal_log(stderr, ALL_LEVEL, "FATAL ERROR: unable to open %s: %s", tmpfname, strerror(errno));
				SjProc->CleanExit(true);
			} 
			internal_log(NULL, ALL_LEVEL, "opened for packets debug: %s successful", tmpfname);
		}

		if (useropt.debug_level >= HACKS_DEBUG) {
			char *tmpfname = (char *)malloc(strlen(useropt.logfname) + 10);
			sprintf(tmpfname, "%s.hacks", useropt.logfname);
			if ((useropt.hacks_logstream = fopen(tmpfname, "a+")) == NULL) {
				internal_log(stderr, ALL_LEVEL, "FATAL ERROR: unable to open %s: %s", tmpfname, strerror(errno));
				SjProc->CleanExit(true);
			}
			internal_log(NULL, ALL_LEVEL, "opened for hacks debug: %s successful", tmpfname);
		}
	}

	SjProc->PrivilegesDowngrade(sjconf->running);

	listening_unix_socket = sj_bind_unixsocket();

	if (sjconf->running->sj_run == false)
		internal_log(NULL, ALL_LEVEL, "sniffjoke is running and INACTIVE: use \"sniffjoke start\" command to start it\n");

	/* main block */
	while (1) {
		mitm->network_io();
		mitm->queue_flush();

		if (mitm->is_network_down()) {
			if (sjconf->running->sj_run == true) {
				internal_log(NULL, ALL_LEVEL, "Network is down, interrupting sniffjoke");
				sjconf->running->sj_run = false;
				restart_on_restore = true;
			}
		} else {
			if (restart_on_restore == true) {
				internal_log(NULL, ALL_LEVEL, "Network restored, restarting sniffjoke");
				sjconf->running->sj_run = true;
				restart_on_restore = false;
			}
		}
		
		sj_srv_child_check_local_unixserv(listening_unix_socket, sjconf);

	}
	/* nevah here */
}
