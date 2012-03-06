/*
 * descriptor_header.h
 *
 *  Created on: Feb 25, 2012
 *      Author: drewkutilek
 */

#ifndef DESCRIPTOR_HEADER_H_
#define DESCRIPTOR_HEADER_H_

#include "payload.h"
#include "util.h"

#define HEADER_SIZE 23

class DescriptorHeader {
private:
	char m_header[HEADER_SIZE];
	MessageId m_message_id;
	unsigned long m_payload_len;
	unsigned short m_time_to_live;
	unsigned short m_hops;
	header_type m_type;
public:
	DescriptorHeader(const char *header);
	DescriptorHeader(header_type type);
	DescriptorHeader(MessageId &messageID, header_type type,
			unsigned short time_to_live, unsigned short hops,
			unsigned long payload_len);
	~DescriptorHeader();
	const char *get_header();
	MessageId& get_message_id();
	header_type get_header_type();
	unsigned short get_time_to_live();
	unsigned short get_hops();
	unsigned long get_payload_len();
};

#endif /* DESCRIPTOR_HEADER_H_ */
