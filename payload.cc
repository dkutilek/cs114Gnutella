/*
 * payload.cc
 *
 *  Created on: Feb 25, 2012
 *      Author: drewkutilek
 */

#include <climits>
#include <string.h>
#include <cstdlib>
#include "payload.h"

#define PONG_LEN 13
#define PUSH_LEN 26

void big_to_little_endian(unsigned long * dest, const char * payload,
		unsigned long len);
void big_to_little_endian(unsigned short * dest, const char * payload,
		unsigned long len);
void little_to_big_endian(char * dest, unsigned long value,
		unsigned long len);
void little_to_big_endian(char * dest, unsigned short value,
		unsigned long len);

/* Payload methods */

Payload::~Payload() {
	free(m_payload);
}

const char *Payload::get_payload() {
	return m_payload;
}

unsigned long Payload::get_payload_len() {
	return m_payload_len;
}

/* Pong_Payload methods */

Pong_Payload::Pong_Payload(unsigned short port, unsigned long ip_addr,
			unsigned long files_shared, unsigned long kilo_shared)
{
	m_payload_len = PONG_LEN;
	m_payload = (char *) malloc(PONG_LEN);

	// Port
	little_to_big_endian(m_payload, port, 2);
	m_port = port;

	// IP Address
	memcpy(m_payload+2, &ip_addr, 4);
	m_ip_addr = ip_addr;

	// Number of Shared Files
	little_to_big_endian(m_payload+6, files_shared, 4);
	m_files_shared = files_shared;

	// Number of Kilobytes Shared
	little_to_big_endian(m_payload+10, kilo_shared, 4);
	m_kilo_shared = kilo_shared;
}

Pong_Payload::Pong_Payload(const char * payload) {
	m_payload_len = PONG_LEN;
	m_payload = (char *) malloc(PONG_LEN);
	memcpy(m_payload, payload, PONG_LEN);

	// Port
	big_to_little_endian(&m_port, m_payload, 2);

	// IP Address
	memcpy(&m_ip_addr, m_payload+2, 4);

	// Number of Shared Files
	big_to_little_endian(&m_files_shared, m_payload+6, 4);

	// Number of Kilobytes Shared
	big_to_little_endian(&m_kilo_shared, m_payload+10, 4);
}

unsigned short Pong_Payload::get_port() {
	return m_port;
}
	
unsigned long Pong_Payload::get_ip_addr() {
	return m_ip_addr;
}

unsigned long Pong_Payload::get_files_shared() {
	return m_files_shared;
}

unsigned long Pong_Payload::get_kilo_shared() {
	return m_kilo_shared;
}

/* Query_Payload methods */

Query_Payload::Query_Payload(unsigned short speed, string search) {
	m_payload_len = 2 + search.length();
	m_payload = (char *) malloc(m_payload_len);

	// Minimum Speed
	little_to_big_endian(m_payload, speed, 2);
	m_speed = speed;

	// Search Criteria
	strcpy(m_payload+2, search.c_str());
	m_search = search;
}

Query_Payload::Query_Payload(const char * payload, unsigned long payload_len) {
	m_payload_len = payload_len;
	m_payload = (char *) malloc(payload_len);
	memcpy(m_payload, payload, payload_len);

	// Minimum Speed
	big_to_little_endian(&m_speed, payload, 2);

	// Search Criteria
	m_search = "";
	for (unsigned long i = 2; i < payload_len && m_payload[i] != 0; i++) {
		m_search.push_back(m_payload[i]);
	}
}

unsigned short Query_Payload::get_speed() {
	return m_speed;
}
	
string Query_Payload::get_search() {
	return m_search;
}

/* Result methods */

Result::Result(unsigned long file_index, unsigned long file_size,
			string file_name)
{
	m_payload_len = 8+file_name.length()+1;
	m_payload = (char *) malloc(m_payload_len);

	// File Index
	little_to_big_endian(m_payload, file_index, 4);
	m_file_index = file_index;

	// File Size
	little_to_big_endian(m_payload+4, file_size, 4);
	m_file_size = file_size;

	// File Name
	strcpy(m_payload+8, file_name.c_str());
	m_payload[m_payload_len] = 0;
	m_file_name = file_name;
}

Result::Result(const char * result, unsigned long length) {
	m_payload_len = length;
	m_payload = (char *) malloc(length);
	memcpy(m_payload, result, length);

	// File Index
	big_to_little_endian(&m_file_index, result, 4);

	// File Size
	big_to_little_endian(&m_file_size, result+4, 4);

	// File Name
	m_file_name = "";
	for (unsigned long i = 2; i < length && result[i] != 0; i++) {
		m_file_name.push_back(result[i]);
	}
}

unsigned long Result::get_file_index() {
	return m_file_index;
}
	
unsigned long Result::get_file_size() {
	return m_file_size;
}

string Result::get_file_name() {
	return m_file_name;
}

/* QueryHit_Payload methods */

QueryHit_Payload::QueryHit_Payload(unsigned short port, unsigned long ip_addr,
			unsigned long speed, vector<Result> result_set,
			const char * servent_id)
{
	// Number of Hits
	m_num_hits = result_set.size();
	little_to_big_endian(m_payload, m_num_hits, 1);

	m_payload_len = 11;
	
	for (unsigned short i; i < m_num_hits; i ++) {
		Result r = result_set.at(i);
		m_payload_len += r.get_payload_len();
	}
	
	m_payload_len += 16;
	m_payload = (char *) malloc (m_payload_len);

	// Port
	little_to_big_endian(m_payload+1, port, 2);
	m_port = port;

	// IP Address
	memcpy(m_payload+3, &ip_addr, 4);
	m_ip_addr = ip_addr;

	// Speed
	little_to_big_endian(m_payload+7, speed, 4);
	m_speed = speed;

	// Result Set
	unsigned long len = 11;
	
	for (unsigned short i = 0; i < m_num_hits; i++) {
		Result r = m_result_set.at(i);
		m_result_set.push_back(r);
		memcpy(m_payload+len, r.get_payload(), r.get_payload_len());
		len += r.get_payload_len();
	}

	// Servent Identifier
	memcpy(m_payload+len, servent_id, 16);
	memcpy(m_servent_id, servent_id, 16);
}

QueryHit_Payload::QueryHit_Payload(const char * payload, unsigned long payload_len) {
	m_payload_len = payload_len;
	m_payload = (char *) malloc (m_payload_len);
	memcpy(m_payload, payload, m_payload_len);

	// Number of Hits
	big_to_little_endian(&m_num_hits, m_payload, 1);

	// Port
	big_to_little_endian(&m_port, m_payload+1, 2);

	// IP Address
	memcpy(&m_ip_addr, m_payload+3, 4);

	// Speed
	big_to_little_endian(&m_speed, m_payload+7, 4);

	// Result Set
	unsigned short len = 11;
	
	for (unsigned short i = 0; i < m_num_hits && len < m_payload_len-16; i++) {
		Result r(m_payload+len, m_payload_len-len-16);
		m_result_set.push_back(r);
		len += r.get_payload_len();
	}

	// Servent Identifier
	memcpy(m_servent_id, m_payload+len, 16);
}

unsigned short QueryHit_Payload::get_num_hits() {
	return m_num_hits;
}

unsigned short QueryHit_Payload::get_port() {
	return m_port;
}
	
unsigned long QueryHit_Payload::get_ip_addr() {
	return m_ip_addr;
}

vector<Result> QueryHit_Payload::get_result_set() {
	return m_result_set;
}

const char *QueryHit_Payload::get_servent_id() {
	return m_servent_id;
}

/* Push_Payload methods */

Push_Payload::Push_Payload(const char * servent_id, unsigned long file_index,
			unsigned long ip_addr, unsigned short port)
{
	m_payload_len = PUSH_LEN;
	m_payload = (char *) malloc(m_payload_len);

	// Servent Identifier
	memcpy(m_payload, servent_id, 16);
	memcpy(m_servent_id, servent_id, 16);

	// File Index
	little_to_big_endian(m_payload+16, file_index, 4);
	m_file_index = file_index;

	// IP Address
	memcpy(m_payload+3, &ip_addr, 4);
	m_ip_addr = ip_addr;

	// Port
	little_to_big_endian(m_payload+24, port, 2);
	m_port = port;
}

Push_Payload::Push_Payload(const char * payload) {
	m_payload_len = PUSH_LEN;
	m_payload = (char *) malloc(m_payload_len);

	// Servent Identifier
	memcpy(m_servent_id, m_payload, 16);

	// File Index
	big_to_little_endian(&m_file_index, m_payload+16, 4);

	// IP Address
	memcpy(&m_ip_addr, m_payload+20, 4);

	// Port
	big_to_little_endian(&m_port, m_payload+24, 2);
}

const char *Push_Payload::get_servent_id() {
	return m_servent_id;
}

unsigned long Push_Payload::get_file_index() {
	return m_file_index;
}

unsigned long Push_Payload::get_ip_addr() {
	return m_ip_addr;
}

unsigned short Push_Payload::get_port() {
	return m_port;
}

void big_to_little_endian(unsigned long * dest, const char * payload,
		unsigned long len) {
	unsigned long result = 0;
	if (len > sizeof (unsigned long))
		len = sizeof (unsigned long);
	for (unsigned long i = 0; i < len; i++) {
		result |= payload[i];
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
