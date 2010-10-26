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

/* 
 * HACK COMMENT:, every hacks require intensive comments because should cause 
 * malfunction, or KILL THE INTERNET :)
 * 
 * A reset must be ignored if the ack value is more than last_ack_seq + window,
 * this is a path due to the denial of service named 
 * "Slipping in the window: TCP Reset attacks", linked below
 * this is another ack working in INNOCENT mode, not with GUILTY/PRESCRIPTION
 *
 * SOURCE: deduction, analysis of the DoS [ http://kerneltrap.org/node/3072 ]
 * VERIFIED IN:
 * KNOW BUGS:
 * WRITTEN IN VERSION: 0.4.0
 *
 */

#include "Packet.h"

class valid_rst_fake_seq : public HackPacket
{
private:
public:
	virtual Packet *createHack(Packet &orig_packet)
	{
		Packet* ret = new Packet(orig_packet);

		ret->resizePayload(0);

		ret->ip->id = htons(ntohs(ret->ip->id) + (random() % 10));
		ret->tcp->seq = htonl(ntohl(ret->tcp->seq) + 65535 + (random() % 12345));
		ret->tcp->window = htons((unsigned short)(-1));
		ret->tcp->rst = ret->tcp->ack = 1;
		ret->tcp->ack_seq = htonl(ntohl(ret->tcp->seq) + 1);
		ret->tcp->fin = ret->tcp->psh = ret->tcp->syn = 0;

		ret->position = ANY_POSITION;

		return ret;
	}

	virtual bool Condition(const Packet &orig_packet)
	{
		return (orig_packet.tcp->ack != 0);
	}

	valid_rst_fake_seq() {
		hackname = "valid_rst_fake_seq";
		hack_frequency = 8;
		prejudge = INNOCENT;
	}

};

extern "C"  HackPacket* CreateHackObject() {
	return new valid_rst_fake_seq;
}