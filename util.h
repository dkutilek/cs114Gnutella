/*
 * util.h
 *
 *  Created on: Mar 5, 2012
 *      Author: drewkutilek
 */

#ifndef UTIL_H_
#define UTIL_H_

#include <string>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;

#define MESSAGEID_LEN 16

// A Gnutella peer. Stores an ip address, port, number of files shared,
// and number of kilobytes shared.
class Peer {
private:
	in_addr_t m_addr;
	in_port_t m_port;
	int m_send, m_recv;
	uint32_t m_numSharedFiles, m_numSharedKilobytes;
public:
	Peer(in_addr_t addr, in_port_t port, int send, int recv,
			uint32_t numSharedFiles, uint32_t numSharedKilobytes);
	Peer(in_addr_t addr, in_port_t port, int send, int recv);
	Peer(in_addr_t addr, in_port_t port);
	Peer();

	bool operator==(const Peer &other) const;
	bool operator!=(const Peer &other) const;
	bool operator <(const Peer &rhs) const;
	bool operator >(const Peer &rhs) const;
	Peer& operator=(const Peer &rhs);

	void set_addr(in_addr_t addr) {m_addr = addr;}
	void set_port(in_port_t port) {m_port = port;}
	void set_send(int send) {m_send = send;}
	void set_recv(int recv) {m_recv = recv;}
	void set_numSharedFiles(uint32_t x) {m_numSharedFiles = x;}
	void set_numSharedKilobytes(uint32_t x) {m_numSharedKilobytes = x;}

	in_addr_t get_addr() const {return m_addr;}
	in_port_t get_port() const {return m_port;}
	int get_send() const {return m_send;}
	int get_recv() const {return m_recv;}
	uint32_t get_numSharedFiles() const {return m_numSharedFiles;}
	uint32_t get_numSharedKilobytes() const {return m_numSharedKilobytes;}

	string getServentID();
};

// The message id in the descriptor header, generated using the ip address,
// port, and number of messages sent by the node so far.
class MessageId {
private:
	char m_id[MESSAGEID_LEN];
public:
	MessageId(Peer& peer, uint32_t * messageCount);
	MessageId(const char * buf);
	MessageId(const MessageId *messageId);
	MessageId();
	MessageId& operator=(const MessageId & rhs);
	bool operator==(const MessageId &other) const;
	bool operator <(const MessageId &rhs) const;
	bool operator >(const MessageId &rhs) const;
	const char * get_id() {return m_id;}
};

enum header_type {
	con, resp, ping, pong, query, queryHit, push, httpget, httpok	//added httpget and httpok types -DG-
};

// Convert header_type to a string
extern string type_to_str(header_type type);

// Get the time as a string
string get_time();

/**
 * This class holds information describing a file that the host is sharing.
 */
class SharedFile {
private:
	string m_filename;
	unsigned int m_bytes;
	unsigned int m_index;	// An identifier for this file
public:
	SharedFile(string filename, unsigned int bytes) {
		m_filename = filename;
		m_bytes = bytes;
	}

	void setFilePath(string filename) { m_filename = filename; }
	void setBytes(unsigned int bytes) { m_bytes = bytes; }
	void setFileIndex(unsigned int index) { m_index = index; }

	unsigned int getFileIndex() { return m_index; }
	string getFileName() { return m_filename; }
	unsigned int getBytes() { return m_bytes; }
};

#endif /* UTIL_H_ */
