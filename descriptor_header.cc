/*
 * descriptor_header.cc
 *
 *  Created on: Feb 25, 2012
 *      Author: drewkutilek
 */

#include "descriptor_header.h"
#include <climits>
#include <string.h>
#include <cstdlib>

using namespace std;

DescriptorHeader::DescriptorHeader(const char *header) {
	memcpy(m_header, header, HEADER_SIZE);

	if (strcmp(m_header, "GNUTELLA OK\n\n") == 0) {
		m_type = resp;
		return;
	}
	if (m_header[16] == (char) 0x10) {
		m_type = con;
		memcpy(&m_port, m_header, 2);
		memcpy(&m_addr, m_header+2, 4);
		//TODO compare version #
		return;
	}

	//unsigned long bit_mask = 0xFF;

	// Message ID
	m_message_id = MessageId(header);

	// Header Type
	switch (m_header[16]) {
	case (char) 0x00:
		m_type = ping;
		break;
	case (char) 0x01:
		m_type = pong;
		break;
	case (char) 0x80:
		m_type = query;
		break;
	case (char) 0x81:
		m_type = queryHit;
		break;
	case (char) 0x40:
		m_type = push;
		break;
	//adding new payload ids for httpget and httpok -DG-
	case (char) 0x12:
		m_type = httpget;
		break;
	case (char) 0x11:
		m_type = httpok;
		break;
	default:
		m_type = none;
		return;
	}

	// Time to Live
	m_time_to_live = m_header[17];

	// Hops
	m_hops = m_header[18];

	// Payload length
	uint32_t len;
	memcpy(&len, m_header+19, sizeof(uint32_t));
	m_payload_len = ntohl(len);
}

DescriptorHeader::DescriptorHeader(header_type type)
{
	/*if (type == con) {
		m_type = con;
		strcpy(m_header, "GNUTELLA CONNECT/0.4\n\n");
		return;
	}*/
	if (type == resp) {
		m_type = resp;
		strcpy(m_header, "GNUTELLA OK\n\n");
		return;
	}

	/* if type == httpget place proper get message
	
	if(type == httpget) {	
		m_type = httpget;
		return;
	}
	if(type == httpok) {	
		m_type = httpok;
		return;
	}*/

}

DescriptorHeader::DescriptorHeader(MessageId &message_id, header_type type,
		unsigned short time_to_live, unsigned short hops,
		uint32_t payload_len)
{
	unsigned long bit_mask = 0xFF;

	// Message ID
	memcpy(m_header, message_id.get_id(), MESSAGEID_LEN);
	m_message_id = message_id;

	// Header Type
	m_type = type;
	switch (m_type) {
	case ping:
		m_header[16] = 0;
		break;
	case pong:
		m_header[16] = 1;
		break;
	case query:
		m_header[16] = 128;
		break;
	case queryHit:
		m_header[16] = 129;
		break;
	case push:
		m_header[16] = 64;
		break;
	// adding new cases for httpget and httpok -DG-
	case httpget:
		m_header[16] = 18;
		break;
	case httpok:
		m_header[16] = 17;
		break;
	default:
		break;
	}

	// Time to Live
	m_time_to_live = time_to_live & bit_mask;
	m_header[17] = m_time_to_live;

	// Hops
	m_hops = hops & bit_mask;
	m_header[18] = m_hops;

	// Payload length
	uint32_t len = htonl(payload_len);
	memcpy(m_header+19, &len, sizeof(uint32_t));
	m_payload_len = payload_len;
}

DescriptorHeader::DescriptorHeader(in_port_t port, in_addr_t addr) {
	memset(m_header, 0, HEADER_SIZE);

	// Type
	m_type = con;
	m_header[16] = 16;
	// Listening Port
	memcpy(m_header, &port, 2);
	m_port = port;
	// Listening Addr
	memcpy(m_header+2, &addr, 4);
	m_addr = addr;
	// Version
	strcpy(m_header+6, "0.4");
}

DescriptorHeader::~DescriptorHeader() {

}

const char *DescriptorHeader::get_header() {
	return m_header;
}

MessageId& DescriptorHeader::get_message_id() {
	return m_message_id;
}

header_type DescriptorHeader::get_header_type() {
	return m_type;
}

unsigned short DescriptorHeader::get_time_to_live() {
	return m_time_to_live;
}

unsigned short DescriptorHeader::get_hops() {
	return m_hops;
}

uint32_t DescriptorHeader::get_payload_len() {
	return m_payload_len;
}

in_port_t DescriptorHeader::get_port() {
	if (m_type == con || 
		m_type == httpget || 
		m_type == httpok)
		return m_port;
	else
		return 0;
}

in_addr_t DescriptorHeader::get_addr() {
	if (m_type == con || 
		m_type == httpget || 
		m_type == httpok)
		return m_addr;
	else
		return 0;
}
