/*
 * payload.cc
 *
 *  Created on: Feb 25, 2012
 *      Author: drewkutilek
 */

#include <string.h>
#include <cstdlib>
#include <cstdio>
#include "payload.h"
#include "util.h"


/* Payload methods */

Payload::Payload() {
	m_payload = NULL;
}

Payload::~Payload() {
	if (m_payload == NULL) {
		free(m_payload);
	}
}

const char *Payload::get_payload() {
	return m_payload;
}

uint32_t Payload::get_payload_len() {
	return m_payload_len;
}

/* Pong_Payload methods */

Pong_Payload::Pong_Payload(in_port_t port, in_addr_t ip_addr,
			uint32_t files_shared, uint32_t kilo_shared)
{
	m_payload_len = PONG_LEN;
	m_payload = (char *) malloc(PONG_LEN);

	// Port
	memcpy(m_payload, &port, 2);
	m_port = port;

	// IP Address
	memcpy(m_payload+2, &ip_addr, 4);
	m_ip_addr = ip_addr;

	// Number of Shared Files
	uint32_t network_files_shared = htonl(files_shared);
	memcpy(m_payload+6, &network_files_shared, 4);
	m_files_shared = files_shared;

	// Number of Kilobytes Shared
	uint32_t network_kilo_shared = htonl(kilo_shared);
	memcpy(m_payload+10, &network_kilo_shared, 4);
	m_kilo_shared = kilo_shared;
}

Pong_Payload::Pong_Payload(const char * payload) {
	m_payload_len = PONG_LEN;
	m_payload = (char *) malloc(PONG_LEN);
	memcpy(m_payload, payload, PONG_LEN);

	// Port
	memcpy(&m_port, m_payload, 2);

	// IP Address
	memcpy(&m_ip_addr, m_payload+2, 4);

	// Number of Shared Files
	uint32_t network_files_shared;
	memcpy(&network_files_shared, m_payload+6, 4);
	m_files_shared = ntohl(network_files_shared);

	// Number of Kilobytes Shared
	uint32_t network_kilo_shared;
	memcpy(&network_kilo_shared, m_payload+10, 4);
	m_kilo_shared = ntohl(network_kilo_shared);
}

in_port_t Pong_Payload::get_port() {
	return m_port;
}
	
in_addr_t Pong_Payload::get_ip_addr() {
	return m_ip_addr;
}

uint32_t Pong_Payload::get_files_shared() {
	return m_files_shared;
}

uint32_t Pong_Payload::get_kilo_shared() {
	return m_kilo_shared;
}

/* Query_Payload methods */

Query_Payload::Query_Payload(uint16_t speed, string search) {
	m_payload_len = 2 + search.length();
	m_payload = (char *) malloc(m_payload_len);

	// Minimum Speed
	uint16_t network_speed = htons(speed);
	memcpy(m_payload, &network_speed, 2);
	m_speed = speed;

	// Search Criteria
	strcpy(m_payload+2, search.c_str());
	m_search = search;
}

Query_Payload::Query_Payload(const char * payload, uint32_t payload_len) {
	m_payload_len = payload_len;
	m_payload = (char *) malloc(payload_len);
	memcpy(m_payload, payload, payload_len);

	// Minimum Speed
	uint16_t network_speed;
	memcpy(&network_speed, m_payload, 2);
	m_speed = ntohs(network_speed);

	// Search Criteria
	m_search = "";
	for (uint32_t i = 2; i < payload_len && m_payload[i] != 0; i++) {
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

Result::Result(uint32_t file_index, uint32_t file_size,
			string file_name)
{
	m_payload_len = 8+file_name.length()+1;
	m_payload = (char *) malloc(m_payload_len);

	// File Index
	uint32_t network_file_index = htonl(file_index);
	memcpy(m_payload, &network_file_index, 4);
	m_file_index = file_index;

	// File Size
	uint32_t network_file_size = htonl(file_size);
	memcpy(m_payload+4, &network_file_size, 4);
	m_file_size = file_size;

	// File Name
	strcpy(m_payload+8, file_name.c_str());
	m_payload[m_payload_len] = 0;
	m_file_name = file_name;
}

Result::Result(const char * result, uint32_t length) {
	m_payload_len = length;
	m_payload = (char *) malloc(length);
	memcpy(m_payload, result, length);

	// File Index
	uint32_t network_file_index;
	memcpy(&network_file_index, result, 4);
	m_file_index = ntohl(network_file_index);

	// File Size
	uint32_t network_file_size;
	memcpy(&network_file_size, result+4, 4);
	m_file_size = ntohl(network_file_size);

	// File Name
	m_file_name = "";
	for (uint32_t i = 8; i < length && result[i] != 0; i++) {
		m_file_name.push_back(result[i]);
	}
}

uint32_t const Result::get_file_index() {
	return m_file_index;
}
	
uint32_t const Result::get_file_size() {
	return m_file_size;
}

string const Result::get_file_name() {
	return m_file_name;
}

/* QueryHit_Payload methods */

QueryHit_Payload::QueryHit_Payload(in_port_t port, in_addr_t ip_addr,
			uint32_t speed, vector<Result> result_set,
			const char * servent_id)
{
	// Number of Hits
	m_num_hits = result_set.size();

	m_payload_len = 11;
	
	for (uint8_t i = 0; i < m_num_hits; i++) {
		Result r = result_set.at(i);
		m_payload_len += r.get_payload_len();
	}
	
	m_payload_len += 16;
	m_payload = (char *) malloc (m_payload_len);

	m_payload[0] = m_num_hits;

	// Port
	memcpy(m_payload+1, &port, 2);
	m_port = port;

	// IP Address
	memcpy(m_payload+3, &ip_addr, 4);
	m_ip_addr = ip_addr;

	// Speed
	uint32_t network_speed = htonl(speed);
	memcpy(m_payload+7, &network_speed, 4);
	m_speed = speed;

	// Result Set
	uint32_t len = 11;
	
	for (unsigned short i = 0; i < m_num_hits; i++) {
		Result r = result_set.at(i);
		m_result_set.push_back(r);
		memcpy(m_payload+len, r.get_payload(), r.get_payload_len());
		len += r.get_payload_len();
	}

	// Servent Identifier
	memcpy(m_payload+len, servent_id, 16);
	memcpy(m_servent_id, servent_id, 16);
}

QueryHit_Payload::QueryHit_Payload(const char * payload, uint32_t payload_len) {
	m_payload_len = payload_len;
	m_payload = (char *) malloc (m_payload_len);
	memcpy(m_payload, payload, m_payload_len);

	// Number of Hits
	m_num_hits = m_payload[0];

	// Port
	memcpy(&m_port, m_payload+1, 2);

	// IP Address
	memcpy(&m_ip_addr, m_payload+3, 4);

	// Speed
	uint32_t network_speed;
	memcpy(&network_speed, m_payload+7, 4);
	m_speed = ntohl(network_speed);

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

in_port_t QueryHit_Payload::get_port() {
	return m_port;
}
	
in_addr_t QueryHit_Payload::get_ip_addr() {
	return m_ip_addr;
}

vector<Result> QueryHit_Payload::get_result_set() {
	return m_result_set;
}

const char *QueryHit_Payload::get_servent_id() {
	return m_servent_id;
}
/* HTTPget_Payload method */

HTTPget_Payload::HTTPget_Payload(uint32_t file_index, uint32_t file_size,
								string file_name)	
{
	//convert file_index to string
	char temp_fi[20];
	sprintf(temp_fi, "%u", file_index);
	string s_file_index = temp_fi;

	//convert file_size to string
	char temp_fs[20];
	sprintf(temp_fs, "%u", file_size);
	string s_file_size = temp_fs;

	//Get /get/12345/
	m_request = "GET /get/" + s_file_index + "/" + file_name 
		+ "/ HTTP/1.0\r\nConnection: Keep-Alive\r\nRange: bytes=0-\r\n\r";

	
	m_payload_len = m_request.length()+1; //additional "\n" added to cstring
	m_payload = (char *) malloc (m_payload_len);

	strcpy(m_payload, m_request.c_str());
	
	
}
HTTPget_Payload::HTTPget_Payload(const char * payload, uint32_t payload_len)
{
	m_payload_len = payload_len;
	m_payload = (char *) malloc(payload_len);
	memcpy(m_payload, payload, payload_len);
	m_request = m_payload;
}
/* HTTPok_Payload method */

HTTPok_Payload::HTTPok_Payload(uint32_t file_size)
{

	//convert file_size to string
	char temp_fs[20];
	sprintf(temp_fs, "%u", file_size);
	string s_file_size = temp_fs;

	//
	m_response = "HTTP 200 OK\r\nServer: Gnutella\r\nContent-type: application/binary\r\nContent-length: "
				+ s_file_size + "\r\n\r\n";

	
	m_payload_len = m_response.length()+1; //additional "\n" added to cstring
	m_payload = (char *) malloc (m_payload_len);

	strcpy(m_payload, m_response.c_str());
		
}
HTTPok_Payload::HTTPok_Payload(const char * payload, uint32_t payload_len)
{
	m_payload_len = payload_len;
	m_payload = (char *) malloc(payload_len);
	memcpy(m_payload, payload, payload_len);
	m_response = m_payload;
	

}

/* Push_Payload methods */

Push_Payload::Push_Payload(const char * servent_id, uint32_t file_index,
		in_port_t port, in_addr_t ip_addr)
{
	m_payload_len = PUSH_LEN;
	m_payload = (char *) malloc(m_payload_len);

	// Servent Identifier
	memcpy(m_payload, servent_id, 16);
	memcpy(m_servent_id, servent_id, 16);

	// File Index
	uint32_t network_file_index = htonl(file_index);
	memcpy(m_payload+16, &network_file_index, 4);
	m_file_index = file_index;

	// IP Address
	memcpy(m_payload+20, &ip_addr, 4);
	m_ip_addr = ip_addr;

	// Port
	memcpy(m_payload+24, &port, 2);
	m_port = port;
}

Push_Payload::Push_Payload(const char * payload) {
	m_payload_len = PUSH_LEN;
	m_payload = (char *) malloc(m_payload_len);

	// Servent Identifier
	memcpy(m_servent_id, m_payload, 16);

	// File Index
	uint32_t network_file_index;
	memcpy(&network_file_index, m_payload+16, 4);
	m_file_index = ntohl(network_file_index);

	// IP Address
	memcpy(&m_ip_addr, m_payload+20, 4);

	// Port
	memcpy(&m_port, m_payload+24, 2);
}

const char *Push_Payload::get_servent_id() {
	return m_servent_id;
}

uint32_t Push_Payload::get_file_index() {
	return m_file_index;
}

in_addr_t Push_Payload::get_ip_addr() {
	return m_ip_addr;
}

in_port_t Push_Payload::get_port() {
	return m_port;
}
