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

	// Message ID
	m_message_id = 0;
	for (int i = 0; i < 16; i++) {
		m_message_id |= m_header[i];
		m_message_id <<= CHAR_BIT;
	}

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
		m_payload_len |= m_header[i];
		m_payload_len <<= CHAR_BIT;
	}
}


DescriptorHeader::DescriptorHeader(unsigned long messageID, header_type type,
		unsigned short time_to_live, unsigned short hops,
		unsigned long payload_len)
{
	unsigned long bit_mask = 0xFF;

	// Message ID
	int i = 0, j = 15;
	while (i < 16) {
		m_header[i] = bit_mask & (messageID >> (j*CHAR_BIT));
		i++;
		j--;
	}
			
	m_message_id = messageID;

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

const char *DescriptorHeader::get_header() {
	return m_header;
}

unsigned long DescriptorHeader::get_message_id() {
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

/*
class DescriptorHeader {
private:
	char m_header[HEADER_SIZE];
	unsigned long m_message_id, m_payload_len;
	unsigned short m_time_to_live, m_hops;
	header_type m_type;
	Payload * m_payload;
public:
	DescriptorHeader(const char * header) {
		memcpy(m_header, header, HEADER_SIZE);

		// Message ID
		m_message_id = 0;
		for (int i = 0; i < 16; i++) {
			m_message_id |= m_header[i];
			m_message_id <<= CHAR_BIT;
		}

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
			m_payload_len |= m_header[i];
			m_payload_len <<= CHAR_BIT;
		}
	}
	DescriptorHeader(unsigned long messageID, header_type type,
			unsigned short time_to_live, unsigned short hops,
			unsigned long payload_len) {
		unsigned long bit_mask = 0xFF;

		// Message ID
		int i = 0, j = 15;
		while (i < 16) {
			m_header[i] = bit_mask & (messageID >> (j*CHAR_BIT));
			i++;
			j--;
		}
		m_message_id = messageID;

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
		case quieryHit:
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

	// Getters
	const char * get_header() {
		return m_header;
	}
	unsigned long get_message_id() {
		return m_message_id;
	}
	header_type get_header_type() {
		return m_type;
	}
	unsigned short get_time_to_live() {
		return m_time_to_live;
	}
	unsigned short get_hops() {
		return m_hops;
	}
	unsigned long get_payload_len() {
		return m_payload_len;
	}
};
*/

