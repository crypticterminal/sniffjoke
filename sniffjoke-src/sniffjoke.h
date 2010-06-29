/*
 * sniffjoke.h, October 2008: 
 * 
 * "does public key and web of trust could create a trustable peer to peer network ?"
 * "yes."
 *
 * how much sniffjoke had to do with p2p/wot ? nothing, but until this kind of 
 * network don't exist, confuse sniffer should be helpfullest!
 */

#ifndef SNIFFJOKE_H
#define SNIFFJOKE_H

#include <net/ethernet.h>

#define STRERRLEN	1024

struct port_range {
	unsigned short start;
	unsigned short end;
};

enum size_buf_t { 
	SMALLBUF = 64, 
	MEDIUMBUF = 256, 
	LARGEBUF = 1024, 
	HUGEBUF = 4096, 
	GARGANTUABUF = 4096 * 4 
};

/* main.cc global functions */
void check_call_ret( const char *, int, int, bool);
void internal_log(FILE *, int, const char *, ...);
#define SJ_PROCESS_TYPE_UNASSIGNED (-1)
#define SJ_PROCESS_TYPE_SRV_FATHER (0)
#define SJ_PROCESS_TYPE_SRV_CHILD (1)
#define SJ_PROCESS_TYPE_CLI (2)
#define SJ_SRV_LOCK "/var/run/sniffjoke/srv.lock"
#define SJ_CLI_LOCK "/var/run/sniffjoke/cli.lock"
#define SJ_SRV_TMPDIR "/var/run/sniffjoke/srv"
#define SJ_SRV_FATHER_PID_FILE SJ_SRV_TMPDIR"/father.pid"
#define SJ_SRV_CHILD_PID_FILE SJ_SRV_TMPDIR"/child.pid"
#define SJ_SRV_US "sniffjoke_srv" // relative to the jail
#define SJ_CLI_US "sniffjoke_cli" // relative to the jail
struct sj_useropt {
	const char *cfgfname;
	const char *user;
	const char *group;
	const char *chroot_dir;
	const char *logfname;
	unsigned int debug_level;
	bool go_foreground;
	bool force_restart;
	FILE *logstream;
	FILE *packet_logstream;
	FILE *hacks_logstream;
};
/* loglevels */
#define ALL_LEVEL       0
#define ALL_LEVEL_NAME	"default"
#define VERBOSE_LEVEL   1
#define VERBOSE_LEVEL_NAME "verbose"
#define DEBUG_LEVEL     2
#define DEBUG_LEVEL_NAME "debug"
#define PACKETS_DEBUG	3
#define PACKETS_DEBUG_NAME "packets"
#define HACKS_DEBUG 	4
#define HACKS_DEBUG_NAME "hacks"

#define MAGICVAL	0xADECADDE
struct sj_config {
	float MAGIC; 				/* integrity check for saved binary configuration */
	bool sj_run; 				/* default: false = NO RUNNING */
	char user[SMALLBUF]; 			/* default: nobody */
	char group[SMALLBUF];			/* default: users */
	char chroot_dir[MEDIUMBUF];		/* default: /var/run/sniffjoke */
	char logfname[MEDIUMBUF];		/* default: /var/log/sniffjoke.log */
	int debug_level;			/* default: 1 */
	char local_ip_addr[SMALLBUF];		/* default: autodetect */
	char gw_ip_addr[SMALLBUF];		/* default: autodetect */
	char gw_mac_str[SMALLBUF];		/* default: autodetect */
	unsigned char gw_mac_addr[ETH_ALEN];	/* the conversion of _str */
	unsigned short max_ttl_probe;		/* default: 26 */
	unsigned short max_session_tracked;	/* default: 20 */
	unsigned short max_packet_que;		/* default: 60 */
	unsigned short max_tracked_ttl;		/* default: 1024 */
	unsigned char interface[SMALLBUF];	/* default: autodetect */
	int tun_number;				/* tunnel interface number */
#define PORTNUMBER 65535
	unsigned char portconf[PORTNUMBER];
#define HEAVY	0x04
#define NORMAL	0x03
#define LIGHT	0x02
#define NONE	0x01

	bool SjH__shift_ack;			/* default false */
	bool SjH__fake_data; 			/* default true */
	bool SjH__fake_seq; 			/* default true */
	bool SjH__fake_close;			/* default true */
	bool SjH__zero_window;			/* default true */
	bool SjH__valid_rst_fake_seq;		/* default true */
	bool SjH__fake_syn;			/* default true */
	bool SjH__half_fake_syn;		/* default false */
	bool SjH__half_fake_ack;		/* default false */
	bool SjH__inject_ipopt;			/* default true */
	bool SjH__inject_tcpopt;		/* default true */

	char *error;
};


class SjConf {
private:
	char io_buf[HUGEBUF];
	const char *resolve_weight_name(int);
public:
	struct sj_config *running;

	void dump_config( const char * );

	char *handle_stat_command(void);
	char *handle_stop_command(void);
	char *handle_start_command(void);
	char *handle_set_command(unsigned short, unsigned short, unsigned char);
	char *handle_showport_command(void);
	char *handle_log_command(int);

	SjConf( struct sj_useropt * );
	~SjConf();
};

#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <sys/poll.h>

/* Maximum Transfert Unit */
#define MTU	1500
enum status_t { ANY_STATUS = 16, SEND = 5, KEEP = 10, YOUNG = 82, DROP = 83 };
enum source_t { ANY_SOURCE = 3, TUNNEL = 80, LOCAL = 5, NETWORK = 13, TTLBFORCE = 28 };
enum proto_t { ANY_PROTO = 11, TCP = 6, ICMP = 9, OTHER_IP = 7 };
enum judge_t { PRESCRIPTION = 10, GUILTY = 315, INNOCENT = 1 };

struct packetblock {
	int orig_pktlen;
	int pbuf_size;
	/* 
 	 * this packet_id are useful for avoid packet duplication
 	 * due to sniffjoke queue, I don't want avoid packet 
 	 * retrasmission (one of TCP best feature :) 
 	 */
	unsigned int packet_id;

	proto_t proto;
	source_t source;
	status_t status;
	judge_t wtf;

	unsigned char pbuf[MTU];
	struct iphdr *ip;
	struct tcphdr *tcp;
	struct icmphdr *icmp;
	unsigned char *payload;

	struct ttlfocus *tf;
};

struct sniffjoke_track {
	unsigned int daddr;
	unsigned short sport;
	unsigned short dport;
	unsigned int isn;
	unsigned int packet_number;
	bool shutdown;

	struct ttlfocus *tf;
};



enum ttlsearch_t { TTL_KNOW = 1, TTL_BRUTALFORCE = 3, TTL_UNKNOW = 9 };
struct ttlfocus {
	unsigned int daddr;
	unsigned char expiring_ttl;
	unsigned char min_working_ttl;
	unsigned char sent_probe;
	unsigned char received_probe;
	unsigned short puppet_port;
	unsigned int rand_key;

	ttlsearch_t status;
};

enum priority_t { HIGH = 188, LOW = 169 };
class TCPTrack {
private:
	/* main function of packet analysis, called by analyze_packets_queue */
	void update_pblock_pointers( struct packetblock * ); 
	void analyze_incoming_icmp( struct packetblock * );
	void analyze_incoming_synack( struct packetblock * );
	void analyze_incoming_rstfin( struct packetblock * );
	void manage_outgoing_packets( struct packetblock * );

	/* functions forging/mangling packets que, ttl analysis */
	void inject_hack_in_queue( struct packetblock *, struct sniffjoke_track * );
	void enque_ttl_probe( struct packetblock *, struct sniffjoke_track * );
	void discern_working_ttl( struct packetblock *, struct sniffjoke_track * );
	unsigned int make_pkt_id( const unsigned char* );
	bool analyze_ttl_stats( struct sniffjoke_track * );
	void mark_real_syn_packets_SEND( unsigned int );

	/* functions for decrete which, and if, inject hacks */
	bool check_uncommon_tcpopt( const struct tcphdr * );
	struct packetblock *packet_orphanotrophy( const struct packetblock *, int );
	bool percentage( float, int );
	float logarithm( int );

	/* the sniffjoke hack apply on the packets */
	void SjH__fake_data( struct packetblock * );
	void SjH__fake_seq( struct packetblock * );
	void SjH__fake_syn( struct packetblock * );
	void SjH__fake_close( struct packetblock * );
	void SjH__zero_window( struct packetblock * );

	/* sadly, those hacks require some analysis */
	void SjH__valid_rst_fake_seq( struct packetblock * );
	void SjH__half_fake_syn( struct packetblock * );
	void SjH__half_fake_ack( struct packetblock * );
	void SjH__shift_ack( struct packetblock * );

	/* size of header to fill with wild IP/TCP options */
#define MAXOPTINJ	12
	void SjH__inject_ipopt( struct packetblock * );
	void SjH__inject_tcpopt( struct packetblock * );

	/* functions required in TCP/IP packets forging */
	unsigned int half_cksum( const void *, int );
	unsigned short compute_sum( unsigned int );
	void fix_iptcp_sum( struct iphdr *, struct tcphdr * );

	/* functions for work in queue and lists */
	struct packetblock *get_free_pblock( int, priority_t, unsigned int );
	void recompact_pblock_list( int );
	struct sniffjoke_track *init_sexion( const struct packetblock * );    
	struct sniffjoke_track *find_sexion( const struct packetblock * );
	struct sniffjoke_track *get_sexion( unsigned int, unsigned short, unsigned short );
	void clear_sexion( struct sniffjoke_track * );
	void recompact_sex_list( int );
	struct ttlfocus *init_ttl_focus( int, unsigned int );
	struct ttlfocus *find_ttl_focus( unsigned int, int );

	int paxmax; 		/* max packet tracked */
	int sextraxmax; 	/* max tcp session tracked */
	int maxttlfocus;	/* max destination ip tracked */
	int maxttlprobe;	/* max probe for discern ttl */

	struct sniffjoke_track *sex_list;
	struct packetblock *pblock_list;
	struct ttlfocus *ttlfocus_list;
	int sex_list_count[2];
	int pblock_list_count[2];

	/* as usually in those classess */
	struct sj_config *runcopy;
public:
	TCPTrack( SjConf* );
	~TCPTrack();
	bool check_evil_packet( const unsigned char * buff, int nbyte);
	void add_packet_queue( const source_t, const unsigned char *, int );
	void analyze_packets_queue();
	struct packetblock *get_pblock( status_t, source_t, proto_t, bool);
	void clear_pblock( struct packetblock * );
	void last_pkt_fix( struct packetblock * );

	/* force all packets sendable, used from NetIO for avoid Sn mangling */
	void force_send(void);
};

class NetIO {
private:
	/* 
 	 * these data are required for handle 
 	 * tunnel/ethernet man in the middle
	 */
	struct sockaddr_ll send_ll;
	struct sj_config *runcopy;
	TCPTrack *conntrack;
public:

	/* tunfd/netfd: file descriptor for I/O purpose */
	int tunfd;
	int netfd;

	/* poll variables, two file descriptors */
	struct pollfd fds[2];

	/* networkdown_condition express if the network is down and sniffjoke must be interrupted 
 * 	 --- but not killed!
 * 	 */
	bool networkdown_condition;

	NetIO( SjConf * );
	~NetIO();
	void network_io();
	void queue_flush();
};

#endif /* SNIFFJOKE_H */
