/*
 * util.cc
 *
 *  Created on: Mar 5, 2012
 *      Author: drewkutilek
 */

#include <climits>
#include <ctime>
#include <string.h>
#include "util.h"
using namespace std;

Peer::Peer(in_addr_t addr, in_port_t port, int socket, unsigned long numSharedFiles,
		unsigned long numSharedKilobytes) {
	m_addr = addr;
	m_port = port;
	m_socket = socket;
	m_numSharedFiles = numSharedFiles;
	m_numSharedKilobytes = numSharedKilobytes;
}

Peer::Peer(in_addr_t addr, in_port_t port, int socket) {
	m_addr = addr;
	m_port = port;
	m_socket = socket;
	m_numSharedFiles = 0;
	m_numSharedKilobytes = 0;
}

Peer::Peer() {
	m_addr = 0;
	m_port = 0;
	m_socket = 0;
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
	this->m_numSharedFiles = rhs.m_numSharedFiles;
	this->m_numSharedKilobytes = rhs.m_numSharedKilobytes;
	return *this;
}

string Peer::getServentID() {
	char addr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &m_addr, addr, INET_ADDRSTRLEN);
	string address(addr);
	string port((const char *)ntohs(m_port));
	string addressAndPort = address + ":" + port;

	unsigned char digest[17];
	MD5((unsigned char *)addressAndPort.c_str(), addressAndPort.length(), digest);
	return string((const char *) digest);
}

MessageId::MessageId(Peer& peer, unsigned long * messageCount) {
	(*messageCount)++;
	memset(m_id, 0, MESSAGEID_LEN);
	time_t cur = time(NULL);
	in_addr_t addr = peer.get_addr();
	in_port_t port = peer.get_port();
	size_t len = 0;
	memcpy(m_id+len, &cur, sizeof(time_t));
	len += sizeof(time_t);
	memcpy(m_id+len, &addr, sizeof(in_addr_t));
	len += sizeof(in_addr_t);
	memcpy(m_id+len, &port, sizeof(in_port_t));
	len += sizeof(in_port_t);
	memcpy(m_id+len, messageCount, sizeof(unsigned long));
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
	case push:
		return "PUSH";
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

void big_to_little_endian(unsigned long * dest, const char * payload,
		unsigned long len) {
	unsigned long result = 0;
	if (len > sizeof (unsigned long))
		len = sizeof (unsigned long);
	for (unsigned long i = 0; i < len; i++) {
		result |= payload[i];
		if (i != len-1)
			result <<= CHAR_BIT;
	}
	*dest = result;
}

void big_to_little_endian(unsigned short * dest, const char * payload,
		unsigned long len) {
	unsigned short result = 0;
	if (len > sizeof (unsigned short))
		len = sizeof (unsigned short);
	for (unsigned long i = 0; i < len; i++) {
		result |= payload[i];
		if (i != len-1)
			result <<= CHAR_BIT;
	}
	*dest = result;
}

void little_to_big_endian(char * dest, unsigned long value,
		unsigned long len) {
	unsigned long i = 0, j = len, bit_mask = 0xFF;
	if (len > sizeof (unsigned long))
		len = sizeof (unsigned long);
	while (i < len) {
		dest[i] = bit_mask & (value >> (j*CHAR_BIT));
		i++;
		j--;
	}
}

void little_to_big_endian(char * dest, unsigned short value,
		unsigned long len) {
	unsigned long i = 0, j = len, bit_mask = 0xFF;
	if (len > sizeof (unsigned short))
		len = sizeof (unsigned short);
	while (i < len) {
		dest[i] = bit_mask & (value >> (j*CHAR_BIT));
		i++;
		j--;
	}
}
