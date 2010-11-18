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

#include "PacketQueue.h"

PacketQueue::PacketQueue() :
	front(new Packet*[2]),
	back(new Packet*[2]),
	cur_prio(LOW),
	cur_pkt(NULL),
	next_pkt(NULL)
{
	debug.log(DEBUG_LEVEL, __func__);

	memset(front, NULL, sizeof(Packet*)*2);
	memset(back, NULL, sizeof(Packet*)*2);	
}


PacketQueue::~PacketQueue(void)
{
	debug.log(DEBUG_LEVEL, __func__);

	get_reset();
	while (get() && cur_pkt != NULL) {
		delete cur_pkt;
	}

	delete[] front;
	delete[] back;
}

void PacketQueue::insert(priority_t prio, Packet &pkt)
{
	delete_if_present(pkt.packet_id);

	if (front[prio] == NULL) {
		pkt.prev = NULL;
		pkt.next = NULL;
		front[prio] = back[prio] = &pkt;
	} else {
		pkt.prev = back[prio];
		pkt.next = NULL;
		back[prio]->next = &pkt;
		back[prio] = &pkt;
	}
}

void PacketQueue::insert_before(Packet &pkt, Packet &ref)
{
	delete_if_present(pkt.packet_id);

	for (int i = HIGH; i <= LOW; i++) {
		if (front[i] == &ref) {
			pkt.prev = NULL;
			pkt.next = &ref;
			ref.prev = &pkt;
			front[i] = &pkt;
			return;
		}
	}
	
	pkt.prev = ref.prev;
	if(ref.prev != NULL)
		ref.prev->next = &pkt;
	pkt.next = &ref;
	ref.prev = &pkt;
}

void PacketQueue::insert_after(Packet &pkt, Packet &ref)
{
	delete_if_present(pkt.packet_id);

	for (int i = HIGH; i <= LOW; i++) {
		if (back[i] == &ref) {
			pkt.prev = &ref;
			pkt.next = NULL;
			ref.next = &pkt;
			back[i] = &pkt;
			return;
		}
	}
	
	pkt.next = ref.next;
	if(ref.next != NULL)
		ref.next->prev = &pkt;
	pkt.prev = &ref;
	ref.next = &pkt;
}

void PacketQueue::remove(const Packet &pkt)
{
	for (int i = HIGH; i <= LOW; i++) {
		if (front[i] == &pkt) {
			if (back[i] == &pkt) {
				front[i] = NULL;
				back[i] = NULL;
			} else {
				front[i] = front[i]->next;
				front[i]->prev = NULL;
			}
			return;
		} else if (back[i] == &pkt) {
			back[i] = back[i]->prev;
			back[i]->next = NULL;
			return;
		}
	}

	if(pkt.prev != NULL)
		pkt.prev->next = pkt.next;

	if(pkt.next != NULL)
		pkt.next->prev = pkt.prev;

	return;
}

void PacketQueue::delete_if_present(unsigned int packet_id)
{
	if (packet_id) { /* HackPackets packets bypass this */
		Packet* tmp = get(packet_id);
		if (tmp != NULL) {
			remove(*tmp);
			delete tmp;
		}
	}	
}

void PacketQueue::get_reset() {
	cur_prio = HIGH;
	cur_pkt = NULL;
	next_pkt = front[HIGH];
}

Packet* PacketQueue::get()
{
	while (1) {
		if (next_pkt != NULL) {
			cur_pkt = next_pkt;
			next_pkt = next_pkt->next;
			break;
		}
		
		if (cur_prio != LOW) {
			cur_prio = LOW;
			next_pkt = front[LOW];
		} else {
			cur_pkt = NULL;
			break;
		}
	}
	
	/* return next_pkt or NULL at the end */
	return cur_pkt;
}

Packet* PacketQueue::get(status_t status, source_t source, proto_t proto) 
{
	while(get() && cur_pkt != NULL) {
		if (status != ANY_STATUS && cur_pkt->status != status)
			continue;

		if (source != ANY_SOURCE && cur_pkt->source != source)
			continue;

		if (proto != ANY_PROTO && cur_pkt->proto != proto)
			continue;

		break;
	}

	/* result if found or NULL if not found */
	return cur_pkt;
}

Packet* PacketQueue::get(unsigned int packet_id)
{
	get_reset();
	while (get() && cur_pkt != NULL) {
		if (cur_pkt->packet_id == packet_id)
			break;
	}

	/* result if found or NULL if not found */
	return cur_pkt;	
}
