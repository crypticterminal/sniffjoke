/*
 *   SniffJoke is a software able to confuse the Internet traffic analysis,
 *   developed with the aim to improve digital privacy in communications and
 *   to show and test some securiy weakness in traffic analysis software.
 *   
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

#include "Packet.h"
#include "HDRoptions.h"

#define MAXIPHEADER 60
#define MAXTCPHEADER 60

Packet::Packet(const unsigned char* buff, int size) :
	prev(NULL),
	next(NULL),
	packet_id(make_pkt_id(buff)),
	evilbit(MORALITYUNASSIGNED),
	source(SOURCEUNASSIGNED),
	status(STATUSUNASSIGNED),
	wtf(JUDGEUNASSIGNED),
	proto(PROTOUNASSIGNED),
	position(POSITIONUNASSIGNED),
	pbuf(size),
        ip(NULL),
        tcp(NULL),
        payload(NULL),
        icmp(NULL)
{
	if(check_evil_packet(buff, size) == false)
		throw exception();

	memcpy(&(pbuf[0]), buff, size);
	updatePacketMetadata();
	
	memset(debug_buf, 0x00, sizeof(debug_buf));
}

Packet::Packet(const Packet& pkt) :
	prev(NULL),
	next(NULL),
	packet_id(0),
	evilbit(MORALITYUNASSIGNED),
	source(SOURCEUNASSIGNED),
	status(STATUSUNASSIGNED),
	wtf(JUDGEUNASSIGNED),
	proto(PROTOUNASSIGNED),
	position(POSITIONUNASSIGNED),
        pbuf(pkt.pbuf),
        ip(NULL),
        tcp(NULL),
        payload(NULL),
        icmp(NULL)
{
	updatePacketMetadata();
	memset(debug_buf, 0x00, sizeof(debug_buf));
}

bool Packet::check_evil_packet(const unsigned char *buff, unsigned int nbyte)
{
	struct iphdr *ip = (struct iphdr *)buff;
 
	if (nbyte < sizeof(struct iphdr) || nbyte < ntohs(ip->tot_len) ) {
		debug.log(PACKETS_DEBUG, "%s %s: nbyte %s < (struct iphdr) %d || (ip->tot_len) %d", 
			__FILE__, __func__, nbyte, sizeof(struct iphdr), ntohs(ip->tot_len)
		);
		return false;
	}

	if (ip->protocol == IPPROTO_TCP) {
		struct tcphdr *tcp;
		int iphlen;
		int tcphlen;

		iphlen = ip->ihl * 4;

		if (nbyte < iphlen + sizeof(struct tcphdr)) {
			debug.log(PACKETS_DEBUG, "%s %s: [bad TCP] nbyte %d < iphlen + (struct tcphdr) %d",
				__FILE__, __func__, nbyte, iphlen + sizeof(struct tcphdr)
			);
			return false;
		}

		tcp = (struct tcphdr *)((unsigned char *)ip + iphlen);
		tcphlen = tcp->doff * 4;
		
		if (ntohs(ip->tot_len) < iphlen + tcphlen) {
			debug.log(PACKETS_DEBUG, "%s %s: [bad TCP][bis] nbyte %d < iphlen + tcphlen %d",
				__FILE__, __func__, nbyte, iphlen + tcphlen
			);
			return false;
		}
	}
	return true;
}

unsigned int Packet::make_pkt_id(const unsigned char* buf) const
{
	struct iphdr *ip = (struct iphdr *)buf;
	struct tcphdr *tcp;
	if (ip->protocol == IPPROTO_TCP) {
		tcp = (struct tcphdr *)((unsigned char *)(ip) + (ip->ihl * 4));
		return tcp->seq;
	} else
		return 0; /* packet_id == 0 mean no ID check */
}

void Packet::mark(source_t source, status_t status, evilbit_t morality)
{
	this->source = source;
	this->status = status;
	this->evilbit = morality;
}

void Packet::mark(source_t source, status_t status, judge_t wtf, evilbit_t morality) {
	this->wtf = wtf;
	mark(source, status, morality);
}

void Packet::updatePacketMetadata()
{	
	/* START INITIAL METADATA RESET */
	ip = NULL;
	iphdrlen = 0; 
	tcp = NULL;
	tcphdrlen = 0;
	payload = NULL;
	datalen = 0;
	icmp = NULL;
	/* END INITIAL METADATA RESET */

	/* START IPHDR UPDATE */
	if(pbuf.size() < sizeof(struct iphdr))
		throw exception();

	ip = (struct iphdr *)&(pbuf[0]);	
	iphdrlen = (ip->ihl * 4);
	/* END IPHDR UPDATE */
	
	switch(ip->protocol) {
		case IPPROTO_TCP:	
			/* START TCPHDR UPDATE */
			if(pbuf.size() < sizeof(struct iphdr) + sizeof(struct tcphdr))
				throw exception();

			proto = TCP;
			tcp = (struct tcphdr *)((unsigned char *)(ip) + iphdrlen);
			tcphdrlen = tcp->doff * 4;
			/* END TCPHDR UPDATE */

			/* START PAYLOAD UPDATE */
			datalen = pbuf.size() - iphdrlen - tcphdrlen;
			if(datalen)
				payload = (unsigned char *)tcp + tcphdrlen;
			/* END PAYLOAD UPDATE */
			
			break;

		case IPPROTO_ICMP: 
			/* START ICMPHDR UPDATE */
			if(pbuf.size() < sizeof(struct iphdr) + sizeof(struct icmphdr))
				throw exception();

			proto = ICMP;
			icmp = (struct icmphdr *)((unsigned char *)(ip) + iphdrlen);
			/* END ICMPHDR UPDATE */
			
			break;
		default:
			proto = OTHER_IP;
			break;
	}
}

unsigned int Packet::half_cksum(const void* data, int len)
{
	const unsigned short *usdata = (const unsigned short *)data;
	unsigned int sum = 0;

	while (len > 1)
	{
		sum += *usdata++;
		if(sum & 0x80000000)
			sum = (sum & 0xFFFF) + (sum >> 16);
		len -= 2;
	}

	if (len == 1)
		sum += (unsigned short) *(unsigned char*)usdata;

	return sum;
}

unsigned short Packet::compute_sum(unsigned int sum)
{
	while(sum>>16)
             sum = (sum & 0xFFFF) + (sum >> 16);

	return (unsigned short) ~sum;
}

void Packet::fixIpTcpSum(void)
{
	unsigned int sum;
	unsigned int l4len = ntohs(ip->tot_len) - iphdrlen;

	ip->check = 0;
	sum = half_cksum((const void *)ip, iphdrlen);
	ip->check = compute_sum(sum);
	tcp->check = 0;
	sum = half_cksum((const void *) &ip->saddr, 8);
	sum += htons (IPPROTO_TCP + l4len);
	sum += half_cksum((const void *)tcp, l4len);
	tcp->check = compute_sum(sum);
}

bool Packet::selfIntegrityCheck(const char *pluginName)
{
	if(source != SOURCEUNASSIGNED ) {
		debug.log(ALL_LEVEL, "selfIntegrityCheck: in %s (source_t)source must not be set: ignored value", pluginName);
	}

	if(status != STATUSUNASSIGNED ) {
		debug.log(ALL_LEVEL, "selfIntegrityCheck: in %s (status_t)status must not be set: ignored value", pluginName);
	}

	if(wtf == JUDGEUNASSIGNED ) {
		debug.log(ALL_LEVEL, "selfIntegrityCheck: in %s not set \"wtf\" field (what the fuck Sj has to do with this packet?)", pluginName);
		goto errorinfo;
	}

	if(proto == PROTOUNASSIGNED) {
		debug.log(ALL_LEVEL, "selfIntegrityCheck: in %s not set \"proto\" field, required", pluginName);
		goto errorinfo;
	}

	if(position == POSITIONUNASSIGNED) {
		debug.log(ALL_LEVEL, "selfIntegrityCheck: in %s not set \"position\" field, required", pluginName);
		goto errorinfo;
	}

	return true;

errorinfo:
	debug.log(DEBUG_LEVEL, "Documentation about plugins development: http://www.sniffjoke.net/delirandom/plugins");
	return false;
}

void Packet::IPHDR_resize(unsigned int size) 
{
	/* safety first! */
	if((pbuf.size() - iphdrlen + size > MTU) || (size < sizeof(struct iphdr)) || (size > MAXIPHEADER))
		SJ_RUNTIME_EXCEPTION();
	
	/* resizing iphdr to a non multiple of 4 it's not safe due to: ip->ihl = size / 4 */
	if(size % 4)
		SJ_RUNTIME_EXCEPTION();
		
	if(iphdrlen == size) /* there is nothing to do in this case */
		return;

	/* its important to update values into hdr before vector insert call because it can cause relocation */
	ip->ihl = (size / 4);

	vector<unsigned char>::iterator it = pbuf.begin();

	if(iphdrlen < size) {
		ip->tot_len = ntohs(pbuf.size() + (size - iphdrlen));
		pbuf.insert(it + iphdrlen, size - iphdrlen, IPOPT_NOOP);

	} else { /* iphdrlen > size */
		ip->tot_len = ntohs(pbuf.size() - (iphdrlen - size));
		pbuf.erase(it + size, it + iphdrlen);
	}
	
	updatePacketMetadata();
}

void Packet::TCPHDR_resize(unsigned int size)
{
	/* safety first! */
	if((pbuf.size() - tcphdrlen + size > MTU) || (size < sizeof(struct tcphdr)) || (size > MAXTCPHEADER))
		SJ_RUNTIME_EXCEPTION();

	/* resizing tcphdr to a non multiple of 4 it's not safe due to: tcp->doff = size / 4 */
	if(size % 4)
		SJ_RUNTIME_EXCEPTION();

	if(tcphdrlen == size) /* there is nothing to do in this case */
		return;
		
	/* its important to update values into hdr before vector insert call because it can cause relocation */
	tcp->doff = (size / 4);
	
	vector<unsigned char>::iterator it = pbuf.begin() + iphdrlen;

	if(tcphdrlen < size) {
		ip->tot_len = ntohs(pbuf.size() + (size - tcphdrlen));
		pbuf.insert(it + tcphdrlen, size - tcphdrlen, TCPOPT_NOP);
	} else { /* tcphdrlen > size */
		ip->tot_len = ntohs(pbuf.size() - (tcphdrlen - size));
		pbuf.erase(it + size, it + tcphdrlen);
	}

	updatePacketMetadata();
}

void Packet::TCPPAYLOAD_resize(unsigned int size)
{
	/* safety first! */
	if(pbuf.size() - datalen + size > MTU)
		SJ_RUNTIME_EXCEPTION();

	if(datalen == size) /* there is nothing to do in this case */
		return;
		
	const unsigned int new_total_len = pbuf.size() - datalen + size;

	/* its important to update values into hdr before vector insert call because it can cause relocation */
	ip->tot_len = ntohs(new_total_len);

	pbuf.resize(new_total_len);
	
	updatePacketMetadata();
}

void Packet::TCPPAYLOAD_fillrandom()
{
	const unsigned diff = pbuf.size() - (iphdrlen + tcphdrlen);
	memset_random(payload, diff);
}

bool Packet::checkIPOPT() {
	unsigned char option;
	unsigned int option_len;
	
	for (unsigned int i = sizeof(struct iphdr); i < iphdrlen;) {
		option = ((unsigned char *)ip)[i];

		switch(option) {
			case IPOPT_END: /* END is stripped off */
				option =  IPOPT_NOOP;
				i++;
				continue;
			case  IPOPT_NOOP:
				i++;
				continue;
			case IPOPT_SSRR:
			case IPOPT_LSRR:
			case IPOPT_RR:
			case IPOPT_RA:
			case IPOPT_CIPSO:
			case IPOPT_SEC:
			case IPOPT_SID:
				goto uncommon_or_invalid_ip_option_present;
/* default_case_checkIPOPT: if you add new options to check use the goto
			    to this label to check the option length */
			default:
				option_len = ((unsigned char *)ip)[i+1];
				if(option_len > (iphdrlen - i)) {
					/* the packet contains invalid options */
					goto uncommon_or_invalid_ip_option_present;
				}
				i += option_len;
		}
	}

	return false;

uncommon_or_invalid_ip_option_present:

	selflog(__func__, "some uncommon or invalid ip option is present!");
	return true;
}

bool Packet::checkTCPOPT() {
	unsigned char option;
	unsigned int option_len;
	
	for (unsigned int i = sizeof(struct tcphdr); i < tcphdrlen;) {
		option = ((unsigned char *)tcp)[i];

		switch(option) {
			case TCPOPT_TIMESTAMP:
				goto default_case_checkTCPOPT;
			case TCPOPT_EOL: // EOL is stripped off
				option = TCPOPT_NOP;
				i++;
				continue;
			case TCPOPT_NOP:
				i++;
				continue;
			case TCPOPT_MAXSEG:
			case TCPOPT_WINDOW:
			case TCPOPT_SACK_PERMITTED:
			case TCPOPT_SACK:
				goto uncommon_or_invalid_tcp_option_present;
default_case_checkTCPOPT: /* if you add new options to check use the goto
			    to this label to check the option length */
			default:
				option_len = ((unsigned char *)tcp)[i+1];
				if(option_len > (tcphdrlen - i)) {
					/* the packet contains invalid options */
					goto uncommon_or_invalid_tcp_option_present;
				}
				i += option_len;
				break;
		}
	}

	return false;

uncommon_or_invalid_tcp_option_present:

	selflog(__func__, "some uncommon or invalid tcp option is present!");
	return true;

}

void Packet::Inject_IPOPT(bool corrupt, bool strip_previous)
{
	/* used to keep track of header growing */
	unsigned int actual_iphdrlen = iphdrlen;

	unsigned int target_iphdrlen = MAXIPHEADER;

	snprintf(debug_buf, sizeof(debug_buf), "__%d__ strip [%d] iphdrlen %d tcphdrlen %d datalen %d pktlen %d", __LINE__, strip_previous, iphdrlen, tcphdrlen, datalen, (int)pbuf.size());
	selflog(__func__, debug_buf);

	if(strip_previous && iphdrlen != sizeof(struct iphdr)) {
		actual_iphdrlen = sizeof(struct iphdr);
		target_iphdrlen = sizeof(struct iphdr) + (random() % (MAXIPHEADER - sizeof(struct iphdr)));
	} else {
		target_iphdrlen = iphdrlen + (random() % (MAXIPHEADER - iphdrlen));
	}

	// iphdrlen must be a multiple of 4
	target_iphdrlen += (target_iphdrlen % 4) ? (4 - target_iphdrlen % 4) : 0;
	IPHDR_resize(target_iphdrlen);

	HDRoptions IPInjector(IPOPTS_INJECTOR, (unsigned char *)ip + sizeof(struct iphdr), actual_iphdrlen, target_iphdrlen);
	int MAXITERATION = 6;

	do {
		IPInjector.randomInjector(corrupt);

	} while( target_iphdrlen != actual_iphdrlen && MAXITERATION-- );
	
	if(target_iphdrlen != actual_iphdrlen) {
		// iphdrlen must be a multiple of 4
		actual_iphdrlen += (actual_iphdrlen % 4) ? (4 - actual_iphdrlen % 4) : 0;
		IPHDR_resize(actual_iphdrlen);
	}

	snprintf(debug_buf, sizeof(debug_buf), "__%d__ strip [%d] iphdrlen %d tcphdrlen %d datalen %d pktlen %d", __LINE__, strip_previous, iphdrlen, tcphdrlen, datalen, (int)pbuf.size());
	selflog(__func__, debug_buf);
}


void Packet::Inject_TCPOPT(bool corrupt, bool strip_previous)
{
	/* used to keep track of header growing */
	unsigned int actual_tcphdrlen = tcphdrlen;
	
	unsigned int target_tcphdrlen = 0;

	snprintf(debug_buf, sizeof(debug_buf), "__%d__ strip [%d] iphdrlen %d tcphdrlen %d datalen %d pktlen %d", __LINE__, strip_previous, iphdrlen, tcphdrlen, datalen, (int)pbuf.size());
	selflog(__func__, debug_buf);
	
	if(strip_previous && tcphdrlen != sizeof(struct tcphdr)) {
		actual_tcphdrlen = sizeof(struct tcphdr);
		target_tcphdrlen = sizeof(struct tcphdr) + (random() % (MAXTCPHEADER - sizeof(struct tcphdr)));
	} else {
		target_tcphdrlen = tcphdrlen + (random() % (MAXTCPHEADER - tcphdrlen));
	}
	
	// tcphdrlen must be a multiple of 4
	target_tcphdrlen += (target_tcphdrlen % 4) ? (4 - target_tcphdrlen % 4) : 0;
	TCPHDR_resize(target_tcphdrlen);
	
	HDRoptions TCPInjector(TCPOPTS_INJECTOR, (unsigned char *)tcp + sizeof(struct tcphdr), actual_tcphdrlen, target_tcphdrlen);
	int MAXITERATION = 6;

	do {
		TCPInjector.randomInjector(corrupt);

	} while( target_tcphdrlen != actual_tcphdrlen && --MAXITERATION ); 


	if(target_tcphdrlen != actual_tcphdrlen) {
		// tcphdrlen must be a multiple of 4
		actual_tcphdrlen += (actual_tcphdrlen % 4) ? (4 - actual_tcphdrlen % 4) : 0;
		TCPHDR_resize(actual_tcphdrlen);
	}

	snprintf(debug_buf, sizeof(debug_buf), "__%d__ strip [%d] iphdrlen %d tcphdrlen %d datalen %d pktlen %d", __LINE__, strip_previous, iphdrlen, tcphdrlen, datalen, (int)pbuf.size());
	selflog(__func__, debug_buf);
}

void Packet::selflog(const char *func, const char *loginfo) 
{
	const char *evilstr, *statustr, *wtfstr, *sourcestr;
	char *p, protoinfo[MEDIUMBUF]; 

	/* inet_ntoa use a static buffer */
	char saddr[MEDIUMBUF], daddr[MEDIUMBUF];

	p = inet_ntoa(*((struct in_addr *)&(ip->saddr)));
	strncpy(saddr, p, sizeof(saddr));

	p = inet_ntoa(*((struct in_addr *)&(ip->daddr)));
	strncpy(daddr, p, sizeof(daddr));

	switch(evilbit) {
		case GOOD: evilstr = "good"; break;
		case EVIL: evilstr = "evil"; break;
                default: case MORALITYUNASSIGNED: evilstr = "unassigned evilbit"; break;

	}

	switch(status) {
		case YOUNG:  statustr = "young"; break;
		case SEND: statustr = "send"; break;
		case KEEP: statustr = "keep"; break;
                default: case STATUSUNASSIGNED: statustr = "unassigned status"; break;
	}

	switch(wtf) {
		case RANDOMDAMAGE: wtfstr ="everybad"; break;
		case PRESCRIPTION: wtfstr ="prescripted"; break;
		case INNOCENT: wtfstr ="innocent"; break;
		case GUILTY: wtfstr ="badchecksum"; break;
		case MALFORMED: wtfstr ="malformed"; break;
                default: case JUDGEUNASSIGNED: wtfstr = "unassigned wtf"; break;
	}

	switch(source) {
		case TUNNEL: sourcestr = "tunnel"; break;
		case LOCAL: sourcestr = "local"; break;
		case NETWORK: sourcestr = "network"; break;
		case TTLBFORCE: sourcestr = "ttl force"; break;
		default: case SOURCEUNASSIGNED: sourcestr = "unassigned source"; break;
	}

	memset(protoinfo, 0x0, sizeof(protoinfo));
	switch(proto) {
		case TCP:
			snprintf(protoinfo, sizeof(protoinfo), "[TCP sp %d dp %d SAFR{%d%d%d%d} len %d(%d) seq %x ack_seq %x]",
				ntohs(tcp->source), ntohs(tcp->dest), tcp->syn, tcp->ack, tcp->fin, 
				tcp->rst, (int)pbuf.size(), (int)(pbuf.size() - iphdrlen - tcphdrlen), 
				ntohl(tcp->seq), ntohl(tcp->ack_seq)
			);
			break;
		case ICMP:
			snprintf(protoinfo, sizeof(protoinfo), "ICMP type %d code %d len %d(%d)",
				icmp->type, icmp->code,
				(int)pbuf.size(), (int)(pbuf.size() - iphdrlen - sizeof(struct icmphdr))
			);
			break;
		case OTHER_IP:
			snprintf(protoinfo, sizeof(protoinfo), "Other proto: %d", ip->protocol);
			break;
		case PROTOUNASSIGNED:
			snprintf(protoinfo, sizeof(protoinfo), "protocol unassigned! value %d", ip->protocol);
			break;
		default: case ANY_PROTO:
			debug.log(ALL_LEVEL, "Invalid and impossibile %s:%d %s", __FILE__, __LINE__, __func__);
			SJ_RUNTIME_EXCEPTION();
			break;
	}

	debug.log(PACKETS_DEBUG, "%s :%x: E|%s status %s WTF|%s src %s|%s->%s proto [%s] ttl %d %s",
		func, packet_id, evilstr, statustr, wtfstr, sourcestr,
		saddr, daddr,
		protoinfo, ip->ttl, loginfo
       	);

	memset(debug_buf, 0x00, sizeof(debug_buf));
}
