/*
 * payload.h
 *
 *  Created on: Feb 25, 2012
 *      Author: drewkutilek
 */

#ifndef PAYLOAD_H_
#define PAYLOAD_H_

#include <string>
#include <stdio.h>
#include <vector>
#include <netinet/in.h>

using namespace std;

#define PONG_LEN 14
#define PUSH_LEN 26

class Payload {
protected:
	char *m_payload;
	uint32_t m_payload_len;
public:
	Payload();
	virtual ~Payload();
	const char *get_payload();
	uint32_t get_payload_len();
};

class Pong_Payload : public Payload {
private:
	in_port_t m_port;
	in_addr_t m_ip_addr;
	uint32_t m_files_shared;
	uint32_t m_kilo_shared;
public:
	Pong_Payload(in_port_t port, in_addr_t ip_addr, uint32_t files_shared,
			uint32_t kilo_shared);
	Pong_Payload(const char *payload);
	in_port_t get_port();
	in_addr_t get_ip_addr();
	uint32_t get_files_shared();
	uint32_t get_kilo_shared();
};

class Query_Payload : public Payload {
private:
	unsigned short m_speed;
	string m_search;
public:
	Query_Payload(unsigned short speed, string search);
	Query_Payload(const char *payload, uint32_t payload_len);
	unsigned short get_speed();
	string get_search();
};

class Result : public Payload {
private:
	uint32_t m_file_index;
	uint32_t m_file_size;
	string m_file_name;
public:
	Result(uint32_t file_index, uint32_t file_size,
			string m_file_name);
	Result(const char *result, uint32_t length);

	uint32_t const get_file_index();
	uint32_t const get_file_size();
	string const get_file_name();
};

class QueryHit_Payload : public Payload {
private:
	unsigned short m_num_hits;
	in_port_t m_port;
	in_addr_t m_ip_addr;
	uint32_t m_speed;
	vector<Result> m_result_set;
	char m_servent_id[16];
public:
	QueryHit_Payload(in_port_t port, in_addr_t ip_addr, uint32_t speed,
			vector<Result> result_set, const char *servent_id);
	QueryHit_Payload(const char *payload, uint32_t payload_len);
	unsigned short get_num_hits();
	in_port_t get_port();
	in_addr_t get_ip_addr();
	vector<Result> get_result_set();
	const char *get_servent_id();
};
class HTTPget_Payload : public Payload {
private: 
	string m_request;
public:
	HTTPget_Payload(uint32_t file_index, uint32_t file_size, string file_name);
	HTTPget_Payload(const char *payload, uint32_t payload_len);
	string get_request() { return m_request; }
};
class HTTPok_Payload : public Payload {
private:
	string m_response;
public:
	HTTPok_Payload(uint32_t file_size);
	HTTPok_Payload(const char *payload, uint32_t payload_len);
	string get_response() { return m_response; }
};	

class Push_Payload : public Payload {
private:
	char m_servent_id[16];
	uint32_t m_file_index;
	in_port_t m_port;
	in_addr_t m_ip_addr;
public:
	Push_Payload(const char *servent_id, uint32_t file_index,
			in_port_t port, in_addr_t ip_addr);
	Push_Payload(const char *payload);
	const char *get_servent_id();
	uint32_t get_file_index();
	in_port_t get_port();
	in_addr_t get_ip_addr();
};

#endif /* PAYLOAD_H_ */
