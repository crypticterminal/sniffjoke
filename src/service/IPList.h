/*
 *   SniffJoke is a software able to confuse the Internet traffic analysis,
 *   developed with the aim to improve digital privacy in communications and
 *   to show and test some securiy weakness in traffic analysis software.
 *   
 *   Copyright (C) 2011 vecna <vecna@delirandom.net>
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

#ifndef SJ_IPLIST_H
#define SJ_IPLIST_H

#include "Utils.h"

#include <map>
#include <memory>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

class IPList
{
public:
    uint32_t ip;
    uint8_t a;
    uint8_t b;
    uint8_t c;

    IPList(uint32_t, uint8_t, uint8_t, uint8_t);
    ~IPList();

    /* utilities */
    void selflog(const char *, const char *) const;
    /* no personal buffer used in selflog, maybe in the future */
};

class IPListMap : public map<uint32_t, IPList*>
{
private:
    const char *dumpfname;
public:
    IPListMap(const char*);
    ~IPListMap();
    IPList& add(uint32_t, uint8_t, uint8_t, uint8_t);
    bool isPresent(uint32_t);
    void load();
    void dump();
};

#endif /* SJ_IPLIST_H */