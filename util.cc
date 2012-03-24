/*
 * util.cc
 *
 *  Created on: Mar 5, 2012
 *      Author: drewkutilek
 */

#include <climits>
#include <ctime>
#include <string.h>
#include <sstream>
#include <cstdio>
#include "util.h"
#include "hash.h"
using namespace std;

Peer::Peer(in_addr_t addr, in_port_t port, int send, int recv,
		uint32_t numSharedFiles, uint32_t numSharedKilobytes) {
	m_addr = addr;
	m_port = port;
	m_send = send;
	m_recv = recv;
	m_numSharedFiles = numSharedFiles;
	m_numSharedKilobytes = numSharedKilobytes;
}

Peer::Peer(in_addr_t addr, in_port_t port, int send, int recv) {
	m_addr = addr;
	m_port = port;
	m_send = send;
	m_recv = recv;
	m_numSharedFiles = 0;
	m_numSharedKilobytes = 0;
}

Peer::Peer(in_addr_t addr, in_port_t port) {
	m_addr = addr;
	m_port = port;
	m_send = -1;
	m_recv = -1;
	m_numSharedFiles = 0;
	m_numSharedKilobytes = 0;
}

Peer::Peer() {
	m_addr = 0;
	m_port = 0;
	m_send = -1;
	m_recv = -1;
	m_numSharedFiles = 0;
	m_numSharedKilobytes = 0;
}

bool Peer::operator ==(const Peer &other) const {
	return this->m_addr == other.m_addr && this->m_port == other.m_port;
}

bool Peer::operator!=(const Peer &other) const {
	return this->m_addr != other.m_addr || this->m_port != other.m_port;
}

bool Peer::operator <(const Peer &rhs) const {
	if (this->m_addr == rhs.m_addr) {
		return this->m_port < rhs.m_port;
	}
	else {
		return this->m_addr < rhs.m_addr;
	}
}

bool Peer::operator >(const Peer &rhs) const {
	if (this->m_addr == rhs.m_addr) {
		return this->m_port > rhs.m_port;
	}
	else {
		return this->m_addr > rhs.m_addr;
	}
}

Peer& Peer::operator =(const Peer &rhs) {
	this->m_addr = rhs.m_addr;
	this->m_port = rhs.m_port;
	this->m_send = rhs.m_send;
	this->m_recv = rhs.m_recv;
	this->m_numSharedFiles = rhs.m_numSharedFiles;
	this->m_numSharedKilobytes = rhs.m_numSharedKilobytes;
	return *this;
}

string Peer::getServentID() {
	char addr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &m_addr, addr, INET_ADDRSTRLEN);
	string address(addr);
	stringstream ss;
	ss << htons(m_port);
	string port(ss.str());
	string addressAndPort = address + ":" + port;

	unsigned char digest[17];
	hash((unsigned char *)addressAndPort.c_str(), addressAndPort.length(), digest);
	ss.flush();
	ss << digest;
	return ss.str();
}

MessageId::MessageId(Peer& peer, uint32_t * messageCount) {
	(*messageCount)++;
	memset(m_id, 0, MESSAGEID_LEN);
	time_t cur = time(NULL);
	in_addr_t addr = peer.get_addr();
	in_port_t port = peer.get_port();
	size_t len = 0;
	memcpy(m_id+len, &cur, sizeof(time_t));
	len += sizeof(time_t);
	if (len + sizeof(in_addr_t) <= MESSAGEID_LEN) {
		memcpy(m_id+len, &addr, sizeof(in_addr_t));
		len += sizeof(in_addr_t);
		if (len + sizeof(in_port_t) <= MESSAGEID_LEN) {
			memcpy(m_id+len, &port, sizeof(in_port_t));
			len += sizeof(in_port_t);
			if (len + sizeof(uint32_t) <= MESSAGEID_LEN) {
				memcpy(m_id+len, messageCount, sizeof(uint32_t));
			}
			else {
				memcpy(m_id+len, messageCount, MESSAGEID_LEN - len);
			}
		}
		else {
			memcpy(m_id+len, &port, MESSAGEID_LEN - len);
		}
	}
	else {
		memcpy(m_id+len, &addr, MESSAGEID_LEN - len);
	}
}

MessageId::MessageId(const char * buf) {
	memcpy(m_id, buf, MESSAGEID_LEN);
}

MessageId::MessageId(const MessageId *messageId) {
	memcpy(m_id, messageId->m_id, MESSAGEID_LEN);
}

MessageId::MessageId() {
	memset(m_id, 0, MESSAGEID_LEN);
}

MessageId& MessageId::operator =(const MessageId &rhs) {
	memcpy(m_id, rhs.m_id, MESSAGEID_LEN);
	return *this;
}

bool MessageId::operator==(const MessageId &other) const {
	for (size_t i = 0; i < 16; i++) {
		if (this->m_id[i] != other.m_id[i])
			return false;
	}
	return true;
}

bool MessageId::operator <(const MessageId &rhs) const {
	for (size_t i = 0; i < 16; i++) {
		if (this->m_id[i] < rhs.m_id[i])
			return true;
		else if (this->m_id[i] > rhs.m_id[i])
			return false;
	}
	return false;
}

bool MessageId::operator >(const MessageId &rhs) const {
	for (size_t i = 0; i < 16; i++) {
		if (this->m_id[i] > rhs.m_id[i])
			return true;
		else if (this->m_id[i] < rhs.m_id[i])
			return false;
	}
	return false;
}

string type_to_str(header_type type) {
	switch (type) {
	case con:
		return "CONNECT";
	case resp:
		return "RESPONSE";
	case ping:
		return "PING";
	case pong:
		return "PONG";
	case query:
		return "QUERY";
	case queryHit:
		return "QUERYHIT";
	case httpget:
		return "HTTPGET";
	case httpok:
		return "HTTPOK";
	case push:
		return "PUSH";
	case none:
		return "NONE";
	}
	return "";
}

string get_time() {
	time_t rawtime;
	struct tm * timeinfo;
	char buffer[80];
	time (&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, 80, "%m/%d/%y %H:%M:%S", timeinfo);

	return string(buffer);
}

string byte_array_to_str(const char * array, uint32_t len) {
	string str;
	char buf[3];
	for (uint32_t i = 0; i < len; i++) {
		sprintf(buf, "%x02", array[i]);
		str.push_back(buf[1]);
		str.push_back(buf[0]);
		if (i+1 != len)
			str.push_back(' ');
	}
	return str;
}
