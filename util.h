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
using namespace std;

#define MESSAGEID_LEN 16

// A Gnutella peer. Stores an ip address, port, number of files shared,
// and number of kilobytes shared.
class Peer {
private:
	in_addr_t m_addr;
	in_port_t m_port;
	unsigned long m_numSharedFiles, m_numSharedKilobytes;
public:
	Peer(in_addr_t addr, in_port_t port, unsigned long numSharedFiles,
			unsigned long numSharedKilobytes);
	Peer(in_addr_t addr, in_port_t port);
	Peer();

	bool operator==(const Peer &other) const;
	bool operator <(const Peer &rhs) const;
	bool operator >(const Peer &rhs) const;
	Peer& operator=(const Peer &rhs);

	void set_addr(in_addr_t addr) {m_addr = addr;}
	void set_port(in_port_t port) {m_port = port;}
	void set_numSharedFiles(unsigned long x) {m_numSharedFiles = x;}
	void set_numSharedKilobytes(unsigned long x) {m_numSharedKilobytes = x;}

	in_addr_t get_addr() const {return m_addr;}
	in_port_t get_port() const {return m_port;}
	unsigned long get_numSharedFiles() const {return m_numSharedFiles;}
	unsigned long get_numSharedKilobytes() const {return m_numSharedKilobytes;}
};

// The message id in the descriptor header, generated using the ip address,
// port, and number of messages sent by the node so far.
class MessageId {
private:
	char m_id[MESSAGEID_LEN];
public:
	MessageId(Peer& peer, unsigned long * messageCount);
	MessageId(const char * buf);
	MessageId(const MessageId *messageId);
	MessageId();
	MessageId& operator=(const MessageId & rhs);
	bool operator==(const MessageId &other) const;
	bool operator <(const MessageId &rhs) const;
	bool operator >(const MessageId &rhs) const;
	const char * get_id() {return m_id;}
};

// Get the time as a string
string get_time();

// Endian conversion
void big_to_little_endian(unsigned long * dest, const char * payload,
		unsigned long len);
void big_to_little_endian(unsigned short * dest, const char * payload,
		unsigned long len);
void little_to_big_endian(char * dest, unsigned long value,
		unsigned long len);
void little_to_big_endian(char * dest, unsigned short value,
		unsigned long len);

#endif /* UTIL_H_ */
