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
	if (strcmp(m_header, "GNUTELLA CONNECT/0.4\n\n") == 0) {
		m_type = con;
		return;
	}

	unsigned long bit_mask = 0xFF;

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
	default:

		break;
	}

	// Time to Live
	m_time_to_live = m_header[17];

	// Hops
	m_hops = m_header[18];

	// Payload length
	m_payload_len = 0;
	for (int i = 19; i < HEADER_SIZE; i++) {
		m_payload_len |= bit_mask & m_header[i];
		if (i != HEADER_SIZE-1)
			m_payload_len <<= CHAR_BIT;
	}
}

DescriptorHeader::DescriptorHeader(header_type type)
{
	if (type == con) {
		m_type = con;
		strcpy(m_header, "GNUTELLA CONNECT/0.4\n\n");
		return;
	}
	if (type == resp) {
		m_type = resp;
		strcpy(m_header, "GNUTELLA OK\n\n");
		return;
	}

}

DescriptorHeader::DescriptorHeader(MessageId &message_id, header_type type,
		unsigned short time_to_live, unsigned short hops,
		unsigned long payload_len)
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
	m_header[19] = bit_mask & (payload_len >> (3*CHAR_BIT));
	m_header[20] = bit_mask & (payload_len >> (2*CHAR_BIT));
	m_header[21] = bit_mask & (payload_len >> (1*CHAR_BIT));
	m_header[22] = bit_mask & (payload_len);
	m_payload_len = payload_len;	
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

unsigned long DescriptorHeader::get_payload_len() {
	return m_payload_len;
}	
