/*
 * payload.h
 *
 *  Created on: Feb 25, 2012
 *      Author: drewkutilek
 */

#ifndef PAYLOAD_H_
#define PAYLOAD_H_

#include <string>
#include <vector>

using namespace std;

class Payload {
protected:
	char *m_payload;
	unsigned long m_payload_len;
public:
	virtual ~Payload();
	const char *get_payload();
	unsigned long get_payload_len();
};

class Pong_Payload : public Payload {
private:
	unsigned short m_port;
	unsigned long m_ip_addr;
	unsigned long m_files_shared;
	unsigned long m_kilo_shared;
public:
	Pong_Payload(unsigned short port, unsigned long ip_addr,
		unsigned long files_shared, unsigned long kilo_shared);
	Pong_Payload(const char *payload);
	unsigned short get_port();
	unsigned long get_ip_addr();
	unsigned long get_files_shared();
	unsigned long get_kilo_shared();
};

class Query_Payload : public Payload {
private:
	unsigned short m_speed;
	string m_search;
public:
	Query_Payload(unsigned short speed, string search);
	Query_Payload(const char *payload, unsigned long payload_len);
	unsigned short get_speed();
	string get_search();
};

class Result : public Payload {
private:
	unsigned long m_file_index;
	unsigned long m_file_size;
	string m_file_name;
public:
	Result(unsigned long file_index, unsigned long file_size, string m_file_name);
	Result(const char *result, unsigned long length);
	unsigned long get_file_index();
	unsigned long get_file_size();
	string get_file_name();
};

class QueryHit_Payload : public Payload {
private:
	unsigned short m_num_hits;
	unsigned short m_port;
	unsigned long m_ip_addr;
	unsigned long m_speed;
	vector<Result> m_result_set;
	char m_servent_id[16];
public:
	QueryHit_Payload(unsigned short port, unsigned long ip_addr,
		unsigned long speed, vector<Result> result_set, const char *servent_id);
	QueryHit_Payload(const char *payload, unsigned long payload_len);
	unsigned short get_num_hits();
	unsigned short get_port();
	unsigned long get_ip_addr();
	vector<Result> get_result_set();
	const char *get_servent_id();
};

class Push_Payload : public Payload {
private:
	char m_servent_id[16];
	unsigned long m_file_index;
	unsigned long m_ip_addr;
	unsigned short m_port;
public:
	Push_Payload(const char *servent_id, unsigned long file_index, unsigned long ip_addr,
		unsigned short port);
	Push_Payload(const char *payload);
	const char *get_servent_id();
	unsigned long get_file_index();
	unsigned long get_ip_addr();
	unsigned short get_port();
};

#endif /* PAYLOAD_H_ */
