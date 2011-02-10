/*
 *   SniffJoke is a software able to confuse the Internet traffic analysis,
 *   developed with the aim to improve digital privacy in communications and
 *   to show and test some securiy weakness in traffic analysis software.
    
 *   Copyright (C) 2010 vecna <vecna@delirandom.net>
 *                      evilaliv3 <giovanni.pellerano@evilaliv3.org>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Debug.h>

Debug::Debug() :
debuglevel(ALL_LEVEL),
logstream(stdout),
session_logstream(stdout),
packet_logstream(stdout)
{
}

void Debug::setLogstream(const char *lsf)
{
    logstream_file = lsf;
    logstream = NULL;
}

void Debug::setSessionLogstream(const char *lsf)
{
    session_logstream_file = lsf;
    session_logstream = NULL;
}

void Debug::setPacketLogstream(const char *lsf)
{
    packet_logstream_file = lsf;
    packet_logstream = NULL;
}

bool Debug::appendOpen(uint8_t thislevel, const char *fname, FILE **previously)
{
    if(*previously == stdout)
        return true;

    if (*previously != NULL)
    {
        log(thislevel, __func__, "requested close of logfile %s (vars used: %s and level %d)",
            fname, fname, thislevel);
        fclose(*previously);
        *previously = NULL;
    }

    if (debuglevel >= thislevel)
    {
        if ((fname == NULL) || ((*previously = fopen(fname, "a+")) == NULL))
            return false;

        log(thislevel, __func__, "opened logfile %s successful with debug level %d", fname, debuglevel);
    }

    return true;
}

bool Debug::resetLevel()
{
    if (!appendOpen(ALL_LEVEL, logstream_file, &logstream))
        return false;

   if (!appendOpen(SESSION_LEVEL, session_logstream_file, &session_logstream))
        return false;

    if (!appendOpen(PACKET_LEVEL, packet_logstream_file, &packet_logstream))
        return false;

    return true;
}

void Debug::log(uint8_t errorlevel, const char *funcname, const char *msg, ...)
{
    if (errorlevel <= debuglevel)
    {
        va_list arguments;
        time_t now = time(NULL);
        FILE *output_flow;

        if (logstream != NULL)
            output_flow = logstream;
        else
            output_flow = stderr;

        if (errorlevel == PACKET_LEVEL && packet_logstream != NULL)
            output_flow = packet_logstream;

        if (errorlevel == SESSION_LEVEL && session_logstream != NULL)
            output_flow = session_logstream;

        char time_str[sizeof ("YYYY-MM-GG HH:MM:SS")];
        strftime(time_str, sizeof (time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

        va_start(arguments, msg);
        fprintf(output_flow, "%s %s: ", time_str, funcname);

        /* the debug level used in development require a pid/uid addictional block */
        if (errorlevel == DEBUG_LEVEL)
            fprintf(output_flow, "%d/%d ", getpid(), getuid());
        /* yes, if you dig in the github, you will discover that this line has been added
         * after one year of developing */

        vfprintf(output_flow, msg, arguments);
        fprintf(output_flow, "\n");
        fflush(output_flow);
        va_end(arguments);
    }
}

void Debug::downgradeOpenlog(uid_t uid, gid_t gid)
{

    /* this should not be called when is not the root process to do */
    if (getuid() && getgid())
        return;

    if (logstream != NULL)
        fchown(fileno(logstream), uid, gid);

    if (packet_logstream != NULL)
        fchown(fileno(packet_logstream), uid, gid);

    if (session_logstream != NULL)
        fchown(fileno(session_logstream), uid, gid);
}

