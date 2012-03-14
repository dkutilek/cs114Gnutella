#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <vector>
#include <map>
#include <set>
#include <cstdlib>
#include <string.h>
#include <string>
#include <fstream>
#include <ctime>
#include <climits>
#include "descriptor_header.h"
#include "payload.h"
#include <dirent.h>	// Directory reading
#include <unistd.h>
#include <poll.h>
#include "util.h"
#include <sys/select.h>
#include <fcntl.h>

#define DEFAULT_PORT 11111
#define BUFFER_SIZE 1024
#define MAX_PEERS 7
#define MAX_PING_STORAGE 14
#define DEFAULT_MAX_UPLOAD_RATE 10
#define DEFAULT_MIN_DOWNLOAD_RATE 10
#define DEFAULT_SHARE_DIRECTORY "./share"
#define DEFAULT_TTL 4
#define DEFAULT_HOPS 0
#define PING_TIMEOUT 5
#define SENDQUERY_TIMEOUT 5
#define CONNECT_TIMEOUT 5
#define PERIODIC_PING 20

using namespace std;

class Gnutella {
private:
	set<Peer> m_peers;  	// A set of peers that this node knows about.
	Peer m_self;			// Contains information about this node.
	fstream m_log;
	unsigned char m_maximumUploadRate;		// in KB/s
	unsigned char m_minimumDownloadRate;	// in KB/s
	string m_sharedDirectoryName;
	vector<string> m_fileList;
	unsigned long m_kilobyteCount;
	unsigned long m_messageCount;
	bool m_userNode;

	// Map from a peer that we forwarded a PING to, to a map from a descriptor
	// ID to a peer who forwarded us the PING. Built when handling PING, used
	// when handling PONG.
	map<Peer,map<MessageId,Peer> > m_sentPingMap;
  
	void error(string msg) {
		m_log << "[ERR " << get_time() << "] " << msg << ": "
				<< strerror(errno) << endl;
		if (m_userNode)
			cerr << "[ERR " << get_time() << "] " << msg << ": "
			<< strerror(errno) << endl;
  	}
  
  	void log(string msg) {
		m_log << "[LOG " << get_time() << "] " << msg << endl;
		if (m_userNode)
			cout << "[LOG " << get_time() << "] " << msg << endl;
  	}

	// Call this function to read the filenames in the directory
	// m_sharedDirectoryName into the vector m_fileList
	void readSharedDirectoryFiles() {
		DIR *dirp = opendir(m_sharedDirectoryName.c_str());
		
		if (dirp == NULL) {
			error("Could not open specified shared directory");
			exit(1);
		}
		
		while (true) {
			struct dirent *entry;
			entry = readdir(dirp);
			
			if (entry == NULL) {
				break;
			}

			string filename(entry->d_name);
			m_fileList.push_back(filename);

			struct stat buf;
			if (stat(filename.c_str(), & buf) == -1) {
				ostringstream oss;
				oss << "Failed to get file size of " << filename;
				error(oss.str());
			}
			else {
				m_kilobyteCount += buf.st_size;
			}
		}
		
		if (closedir(dirp)) {
			error("Could not close directory");
			exit(1);
		}
	}
	
	// Generate a unique message id using the port
	// and number of messages sent.
	MessageId generateMessageId() {
		return MessageId(m_self, &m_messageCount);
	}

	/**
	 * This function is used to acquire a socket to listen to incoming 
	 * connections on.  Returns the socket descriptor.
	 */
	int acquireListenSocket(unsigned short port) {
		int sock = socket(PF_INET, SOCK_STREAM, 0);

		if (sock == -1) {
			error("Could not acquire listening socket");
			exit(1);
		}

		// Reuse addresses that are in TIME_WAIT
		int tr = 1;
		if (setsockopt(sock, SOL_SOCKET,SO_REUSEADDR, &tr, sizeof(int)) 
			== -1) 
		{
			error("Could not set socket option");
			exit(1);
		}

		sockaddr_in nodeInfo;
		memset(&nodeInfo, 0, sizeof(nodeInfo));
		nodeInfo.sin_family = AF_INET;
		nodeInfo.sin_addr.s_addr = INADDR_ANY;
		nodeInfo.sin_port = htons(port);

		int status = bind(sock, (sockaddr *) &nodeInfo, sizeof(nodeInfo));

		if (status == -1) {
			error("Could not bind listening socket");
			exit(1);
		}

		status = listen(sock, 10000);

		if (status == -1) {
			error("Could not listen on socket");
			exit(1);
		}

		return sock;
	}

	/**
	 * This function is used to acquire a socket for starting a connection to
	 * a remote peer.  Returns the socket descriptor.
	 */
	int acquireSendSocket() {
		int sock = socket(PF_INET, SOCK_STREAM, 0);

		if (sock == -1) {
			error("Could not acquire send socket");
			return sock;
		}

		int tr = 1;
		if (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&tr,sizeof(int)) == -1) {
			error("Could not set socket option");
			return -1;
		}

		return sock;
	}

	/**
	 * This function polls the listening port to see if someone is attempting
	 * to connect to this node.  The optional argument timeout determines how
	 * long to wait for a connection.  The default value 0 causes the function
	 * to block indefinitely.  Returns the connection handle or -1 if failed.
	 */
	int getConnectionRequest(sockaddr_in *remoteInfo,
			unsigned int timeout = 0)
	{
		memset(remoteInfo, 0, sizeof(sockaddr_in));
		socklen_t addrLength = sizeof (sockaddr);

		if (timeout != 0) {
			pollfd pfd;
			pfd.fd = m_self.get_socket();
			pfd.events = POLLIN;

			int retval = poll(&pfd, 1, timeout*100);

			if (retval == -1) {
				error("Poll on listening port failed");
				return -1;
			}
			else if (retval == 0) {
				return -1;
			}
			else {
			 	return accept(m_self.get_socket(), (sockaddr *) remoteInfo,
			 			&addrLength);
			}
		}
		else {
			return accept(m_self.get_socket(), (sockaddr *) remoteInfo,
					&addrLength);
		}
	}
	
	/**
	 * This function reads a descriptor header from a peer connection, and
	 * returns a new DescriptorHeader object.  Make sure to delete it when
	 * you are done with it.
	 */
	DescriptorHeader *readDescriptorHeader(Peer peer) {
		char buffer[HEADER_SIZE];
		memset(buffer, 0, HEADER_SIZE);
		int used = 0;
		int remaining = HEADER_SIZE;
		
		time_t starttime = 0;

		while (remaining > 0) {
			// Timeout on the recv if EAGAIN or EWOULBLOCK has been
			// seen from running recv with MSG_DONTWAIT
			if (starttime != 0 &&
				difftime(time(NULL), starttime) > CONNECT_TIMEOUT)
				return NULL;

			int bytesRead = recv(peer.get_socket(), &buffer[used], remaining,
					MSG_DONTWAIT);

			if (bytesRead < 0) {
				// recv set errno to EAGAIN or EWOULDBLOCK, so start the
				// connection timeout.
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					if (starttime == 0)
						starttime = time(NULL);
					continue;
				}
				else
					return NULL;
			}

			used += bytesRead;
			remaining -= bytesRead;
		}
		
		return new DescriptorHeader(buffer);
	}
	
	/**
	 * This function reads a descriptor payload based on it's size described 
	 * in the descriptor header.  Make sure to delete it when you are done 
	 * with it.
	 */
	Payload *readDescriptorPayload(Peer peer, DescriptorHeader header) {
		int payloadSize = header.get_payload_len();

		if (payloadSize < 1) {
			log("Invalid payload size");
			return NULL;
		}
		
		char *buffer = new char[payloadSize];
		memset(buffer, 0, payloadSize);
		int used = 0;
		int remaining = payloadSize - 1;
		
		time_t starttime = 0;

		while (remaining > 0) {
			// Timeout on the recv if EAGAIN or EWOULBLOCK has been
			// seen from running recv with MSG_DONTWAIT
			if (starttime != 0
				&& difftime(time(NULL), starttime) > CONNECT_TIMEOUT) {
				delete buffer;
				return NULL;
			}

			int bytesRead = recv(peer.get_socket(), &buffer[used], remaining,
					MSG_DONTWAIT);

			if (bytesRead < 0) {
				// recv set errno to EAGAIN or EWOULDBLOCK, so start the
				// connection timeout.
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					if (starttime == 0)
						starttime = time(NULL);
					continue;
				}
				else {
					delete buffer;
					return NULL;
				}
			}

			used += bytesRead;
			remaining -= bytesRead;
			buffer[used] = '\0';
		}

		Payload *payload;

		switch (header.get_header_type()) {
		case ping:
			payload = NULL;
			break;
		case pong:
			payload = new Pong_Payload(buffer);
			break;
		case query:
			payload = new Query_Payload(buffer, payloadSize);
			break;
		case queryHit:
			payload = new QueryHit_Payload(buffer, payloadSize);
			break;
		case push:
			payload = new Push_Payload(buffer);
			break;
		default:
			payload = NULL;
			break;
		}

		delete[] buffer;

		return payload;
	}
	
	// This function will set up a sockaddr_in structure for initializing
	// a connection to a remote node
	void remoteInfoSetup(sockaddr_in *remoteInfo, const char *address, 
		unsigned short port)
	{
		memset(remoteInfo, 0, sizeof(sockaddr_in));
		remoteInfo->sin_family = AF_INET;
		remoteInfo->sin_addr.s_addr = inet_addr(address);
		remoteInfo->sin_port = htons(port);
	}
	
	// This function will set up a sockaddr_in structure for initializing
	// a connection to a remote node
	void remoteInfoSetup(sockaddr_in *remoteInfo, in_addr_t address,
		in_port_t port)
	{
		memset(remoteInfo, 0, sizeof(sockaddr_in));
		remoteInfo->sin_family = AF_INET;
		remoteInfo->sin_addr.s_addr = address;
		remoteInfo->sin_port = port;
	}

	/*

	// This function gets called when the node receives a "GNUTELLA CONNECT"
	// message.  It just sends a "GNUTELLA OK" response back.
	void handleConnect(in_addr address, in_port_t port) {
		DescriptorHeader header(resp);
		unsigned short p = ntohs(port);
		sendToAddrPort(address, htons(p-1), &header, NULL);
	}

	*/

	// This function gets called when the node receives a PING message.
	void handlePing(DescriptorHeader *header, Peer peer) {
		bool sent = false;

		DescriptorHeader d(header->get_message_id(), 
				header->get_header_type(), header->get_time_to_live()-1,
				header->get_hops()+1, header->get_payload_len());

		// Pass PING along to all our peers
		for (set<Peer>::iterator it = m_peers.begin(); it != m_peers.end();
				it++)
		{
			if ((*it) == peer) {
				sendPong(*it, header->get_message_id());
				sent = true;
			}
			else {
				map<Peer,map<MessageId,Peer> >::iterator x =
						m_sentPingMap.find(*it);
				// If there isn't a map from the message id to a peer already
				// associated with this peer
				if (x != m_sentPingMap.end()) {
					map<MessageId,Peer>::iterator y =
							x->second.find(header->get_message_id());
					if (y == x->second.end()) {
						// Clear out old PINGs
						while(x->second.size() >= MAX_PING_STORAGE)
							x->second.erase(x->second.begin());
						x->second.insert(pair<MessageId,Peer>
								(header->get_message_id(),peer));
						// Forward the PING
						sendToPeer(*it, &d, NULL);
					}
				}
				// There wasn't a map for this peer at all
				else {
					map<MessageId,Peer> idToPeer;
					idToPeer.insert(pair<MessageId,Peer>
							(header->get_message_id(),peer));
					m_sentPingMap.insert(pair<Peer,map<MessageId,Peer> >
							(*it,idToPeer));
					// Forward the PING
					sendToPeer(*it, &d, NULL);
				}
			}
		}

		// Add to our list of peers if we can
		set<Peer>::iterator setIter = m_peers.find(peer);
		if (!sent && setIter == m_peers.end() && m_peers.size() < MAX_PEERS) {
			ostringstream oss;
			oss << "New peer at " << ntohs(peer.get_port()) << " added";
			log(oss.str());
			m_peers.insert(peer);
			sendPong(peer, header->get_message_id());
		}
	}


	// This function gets called when the node receives a PONG message.
	void handlePong(DescriptorHeader *header, Peer peer) {
		// Get the payload
		Pong_Payload *payload = (Pong_Payload *) 
				readDescriptorPayload(peer, *header);

		if (payload == NULL)
			return;

		// Check if we need to pass this PONG along
		map<Peer,map<MessageId,Peer> >::iterator x = m_sentPingMap.find(peer);
		
		if (x != m_sentPingMap.end()) {
			map<MessageId,Peer>::iterator y = 
					x->second.find(header->get_message_id());
					
			if (y != x->second.end()) {
				// We might be the actual target
				if (y->second == m_self) {
					/*Peer new_peer(payload->get_ip_addr(), payload->get_port(),
							0,
							payload->get_files_shared(),
							payload->get_kilo_shared());

					if (new_peer != m_self) {
						if (m_peers.find(new_peer) != m_peers.end()) {
							ostringstream oss;
							oss << "Updated peer at "
									<< ntohs(payload->get_port());
							log(oss.str());
							m_peers.insert(new_peer);
						}
						else if (m_peers.size() < MAX_PEERS){
							ostringstream oss;
							oss << "New peer at " << ntohs(payload->get_port())
									<< " added";
							log(oss.str());
							m_peers.insert(new_peer);
						}
					}*/

					int sock = acquireSendSocket();
					Peer new_peer(payload->get_ip_addr(), payload->get_port(),
							sock, payload->get_files_shared(),
							payload->get_kilo_shared());
					set<Peer>::iterator iter = m_peers.find(new_peer);

					// Connect to the peer if we don't already know about it
					// and we can accept more peers.
					if (iter == m_peers.end() && m_peers.size() < MAX_PEERS) {
						gnutellaConnect(new_peer);
					}
					else {
						close(sock);
					}
				}
				// If not send it along
				else {
					DescriptorHeader d(header->get_message_id(),
							header->get_header_type(),
							header->get_time_to_live()-1, header->get_hops()+1,
							header->get_payload_len());
					sendToPeer(y->second, &d, payload);

					// Erase saved PING
					// x->second.erase(y->first);
				}
			}
		}
		delete payload;
	}

	// This function gets called when the node receives a QUERY message.
	void handleQuery(DescriptorHeader *header, Peer peer) {
		// Get the payload
		Query_Payload *payload = (Query_Payload *)
				readDescriptorPayload(peer, *header);

		if (payload == NULL)
			return;

		// The first character of the payload should be the transfer rate
		//unsigned short transferRate = payload->get_speed();

		// The rest of the payload is a string that determines the file that
		// the sender is looking for
		//string searchCriteria = payload->get_search();

		delete payload;
	}

	// This function gets called when the node receives a QUERYHIT message.
	void handleQueryHit(DescriptorHeader *header, Peer peer) {
		// Get the payload
		QueryHit_Payload *payload = (QueryHit_Payload *)
				readDescriptorPayload(peer, *header);

		if (payload == NULL)
			return;

		// Do things with the QUERYHIT

		delete payload;
	}

	// This function gets called when the node receives a PUSH message.
	void handlePush(DescriptorHeader *header, Peer peer) {
		// Get the payload
		Push_Payload *payload = (Push_Payload *)
				readDescriptorPayload(peer, *header);

		if (payload == NULL)
			return;

		// Do things with the PUSH

		delete payload;
	}

	/**
	 * This function will try to connect to a peer using the gnutella protocol.
	 * If the remote peer responds correctly, it will be added to the list of
	 * known peers.
	 */
	void gnutellaConnect(Peer peer) {
	  	// Try to connect through TCP to peer.
	  	if (connectToPeer(peer)) {
	  		// Send a connect message
	  		DescriptorHeader request(m_self.get_port(), m_self.get_addr());
	  		sendToPeer(peer, &request, NULL);

	  		// Get the response
	  		DescriptorHeader *response;
	  		response = readDescriptorHeader(peer);

	  		// If it returned null, try again
	  		while (response == NULL) {
	  			response = readDescriptorHeader(peer);
	  		}

	  		// If it's the correct response, try to add to our list of peers
	  		if (response != NULL && response->get_header_type() == resp) {
	  			log("Connected to peer.");

	  			set<Peer>::iterator iter = m_peers.find(peer);

	  			// Make sure the peer isn't already known, and we have room
	  			// for it
	  			if (iter == m_peers.end() && m_peers.size() < MAX_PEERS) {
	  				ostringstream oss;
	  				oss << "New peer at " << ntohs(peer.get_port()) << " added";
	  				log(oss.str());

	  				m_peers.insert(peer);
	  			}
	  			else {
	  				close(peer.get_socket());
	  			}
	  		}
	  		else {
	  			log("Peer rejected CONNECT request.");
	  			close(peer.get_socket());
	  		}

	  		delete response;
	  	}
	}

	// Send a PONG to a given peer
	void sendPong(Peer peer, MessageId& messageId) {
		DescriptorHeader header(messageId, pong, DEFAULT_TTL,
								DEFAULT_HOPS, PONG_LEN);
		Pong_Payload payload(m_self.get_port(), m_self.get_addr(),
				m_fileList.size(), m_kilobyteCount);

		sendToPeer(peer, &header, &payload);
	}

	/*
	void sendToAddrPort(in_addr addr, in_port_t port, DescriptorHeader* header,
			Payload* payload) {
		Peer peer(addr.s_addr, port);
		sendToPeer(peer, header, payload);
	}
	*/

	// Attempt to connect to a peer for CONNECT_TIMEOUT seconds
	bool connectToPeer(Peer peer) {
		// Set up the structure for connecting to the remote node
		sockaddr_in nodeInfo;
		remoteInfoSetup(&nodeInfo, peer.get_addr(), peer.get_port());

		// Try to start a connection
		int status;
		time_t starttime = time(NULL);
		while ((status = connect(peer.get_socket(), (sockaddr *) &nodeInfo,
				sizeof(nodeInfo))) == -1 &&
				difftime(time(NULL), starttime) < CONNECT_TIMEOUT
				&& errno == EADDRINUSE){};
		if (status == -1) {
			ostringstream oss;
			oss << "Could not connect to peer at " << ntohs(peer.get_port());
			error(oss.str());
			return false;
		}
		return true;
	}

	// Send a header and a payload to a given peer.  This will only work if the
	// node is not already actively listening on m_socket.  For CONNECT,
	// RESPONSE, and PING headers, payload = NULL.
	void sendToPeer(Peer peer, DescriptorHeader* header, Payload* payload) {
		// Send a header followed by a payload
		ostringstream oss;
		oss << "Sending " << type_to_str(header->get_header_type()) <<
				" request to peer at " << ntohs(peer.get_port());
		log(oss.str());

		int status = send(peer.get_socket(), header->get_header(), HEADER_SIZE, 0);

		if (status != HEADER_SIZE) {
			ostringstream logoss;
			logoss << "Failed to send "
					<< type_to_str(header->get_header_type()) <<
					" request to peer at " << ntohs(peer.get_port());
			error(logoss.str());
			return;
		}

		if (payload != NULL) {
			status = send(peer.get_socket(), payload->get_payload(),
					payload->get_payload_len(), 0);

			if (status < 0 ||
					(unsigned int) status != payload->get_payload_len()) {
				ostringstream logoss;
				logoss << "Failed to send "
						<< type_to_str(header->get_header_type())
						<< " payload to peer at " << ntohs(peer.get_port());
				error(logoss.str());
				return;
			}
		}
	}

	// Send a header and a payload over an already opened connection.
	void sendOverConnection(int connection, DescriptorHeader* header,
			Payload* payload) {
		int status = send(connection, header->get_header(), HEADER_SIZE, 0);
		if (status == -1) {
			ostringstream logoss;
			logoss << "Failed to send "
					<< type_to_str(header->get_header_type())
					<< " request to peer at on connection " << connection;
			error(logoss.str());
			return;
		}
		if (payload != NULL) {
			status = send(connection, payload->get_payload(),
					payload->get_payload_len(), 0);
			if (status < 0 ||
					(unsigned int) status != payload->get_payload_len()) {
				ostringstream logoss;
				logoss << "Failed to send "
						<< type_to_str(header->get_header_type())
						<< " payload to peer on connection " << connection;
				error(logoss.str());
				return;
			}
		}
	}

public:
  	Gnutella(int port = DEFAULT_PORT, bool userNode = false) {
		m_sharedDirectoryName = DEFAULT_SHARE_DIRECTORY;
		m_messageCount = 0;
		m_userNode = userNode;
		m_kilobyteCount = 0;
		
		// Open the log
		char str[15];
		sprintf(str,"logs/log_%d",port);
		m_log.open(str,ios::out);
		
		if(!m_log.is_open()) {
	  		cerr << "Could not open log file: " << strerror(errno) << endl;
	  		exit(1);
		}
		
		// Get the list of filenames that this node is willing to share
		readSharedDirectoryFiles();

		// Acquire the listen socket
		int sock = acquireListenSocket(port);

		// Store this node's information
		m_self = Peer(inet_addr("127.0.0.1"), htons(port), sock,
				(unsigned long) m_fileList.size(), m_kilobyteCount);
		m_self.set_socket(sock);
  	}

  	~Gnutella() {
		m_log.close();
  	}

  	void activateUserNode() {
  		m_userNode = true;
  	}

  	/**
  	 * This function checks if the connection on the socket associated
  	 * with a peer has been closed from the remote end.
  	 */
  	bool connectionClosed(Peer peer) {
  		char buffer[HEADER_SIZE];
  		int bytesRead = recv(peer.get_socket(), buffer, HEADER_SIZE, MSG_PEEK);

  		return bytesRead == 0;
  	}

	/**
	 * This function handles requests sent to this node from other peers
	 * with open connections.
	 */
  	void handleRequest(Peer peer) {
  		// Construct a descriptor header from the message
		DescriptorHeader *header = readDescriptorHeader(peer);

		if (header == NULL) {
			return;
		}

		if (header->get_header_type() != con && header->get_time_to_live() == 0) {
			return;
		}

		ostringstream oss;
		// Handle requests
		switch (header->get_header_type()) {
		case con:
			// do nothing, an existing peer should not be sending this message.
			break;
		case ping:
			oss << "Received PING from peer at " << ntohs(peer.get_port());
			log(oss.str());
			handlePing(header, peer);
			break;
		case pong:
			oss << "Received PONG from peer at " << ntohs(peer.get_port());
			log(oss.str());
			handlePong(header, peer);
			break;
		case query:
			oss << "Received QUERY from peer at " << ntohs(peer.get_port());
			log(oss.str());
			//handleQuery(connection, header,remoteInfo.sin_addr, remoteInfo.sin_port);
			break;
		case queryHit:
			oss << "Received QUERYHIT from peer at " << ntohs(peer.get_port());
			log(oss.str());
			//handleQueryHit(connection, header,remoteInfo.sin_addr, remoteInfo.sin_port);
			break;
		case push:
			oss << "Received PUSH from peer at " << ntohs(peer.get_port());
			log(oss.str());
			//handlePush(connection, header,remoteInfo.sin_addr, remoteInfo.sin_port);
			break;
		case resp:
			// Do nothing
			break;
		}

		delete header;
  	}


  	/**
  	 * This function is called when the node wants to wait and listen for
  	 * connections or messages from existing connections. The optional
  	 * timeout will cause the function to terminate after the timeout has
  	 * expired.
  	 */
  	void acceptConnections(unsigned int timeout = 0) {
  		time_t starttime = time(NULL);

  		while (timeout == 0 || difftime(time(NULL), starttime) < timeout) {
  			// Check if there are connections to accept
  			sockaddr_in remoteInfo;
  			int connection = getConnectionRequest(&remoteInfo, 1);

  			// Connection accepted, check if they sent a CONNECT request
  			if (connection != -1) {
  				Peer peer(remoteInfo.sin_addr.s_addr, remoteInfo.sin_port,
  						connection);
  				DescriptorHeader *message = readDescriptorHeader(peer);

  				if (message != NULL && message->get_header_type() == con) {
  					ostringstream connect_oss;
  					connect_oss << "Received CONNECT from peer at " <<
							ntohs(message->get_port()) << ". ";
  					log(connect_oss.str());
					
  					// Place listening addr and port from header into
  					// new peer object
  					peer.set_port(message->get_port());
  					peer.set_addr(message->get_addr());

  					// Check if we can add the peer
  					set<Peer>::iterator setIter = m_peers.find(peer);

  					if (setIter == m_peers.end() && 
						m_peers.size() < MAX_PEERS)
					{
  						// Send an acknowledgement of the CONNECT request
  						DescriptorHeader response(resp);
  						sendToPeer(peer, &response, NULL);

  						ostringstream new_peer_oss;
  					  	new_peer_oss << "Adding new peer.";
  					  	log(new_peer_oss.str());
  					  	m_peers.insert(peer);
  					}
  					// Else the peer is already known, or max peers have been
  					// reached
  					else {
  						// Reject the connection
  						close(connection);
  					}
  				}

  				delete message;
  			}
  			else {
  				// Check if any peers sent data on their connections
  				pollfd *pfds;
  				pfds = new pollfd[m_peers.size()];
  				int i = 0;

  				for (set<Peer>::iterator iter = m_peers.begin();
  						iter != m_peers.end(); ++iter)
  				{
  					pfds[i].fd = iter->get_socket();
  					pfds[i].events = POLLIN;
  					i++;
  				}

  				int rv = poll(pfds, m_peers.size(), timeout * 100);
  				i = 0;

  				if (rv > 0) {
  					for (set<Peer>::iterator iter = m_peers.begin();
  							iter != m_peers.end(); ++iter)
  					{
  						if (pfds[i].fd == iter->get_socket() &&
  								(pfds[i].revents & POLLIN))
  						{
  							// If the remote end closed the connection, close
  							// this end's socket and delete the peer.
  							if (connectionClosed(*iter)) {
  								ostringstream oss;
  								oss << "Peer " << ntohs(iter->get_port()) <<
  										" closed the connection.";
  								log(oss.str());

  								close(iter->get_socket());
  								m_peers.erase(iter);
  							}
  							else {
  								handleRequest(*iter);
  							}

  							i++;
  						}
  					}
  				}

  				delete[] pfds;

  				/*
  				fd_set fds;
  				FD_ZERO(&fds);
  				int n = 0;

  				for (set<Peer>::iterator iter = m_peers.begin();
  						iter != m_peers.end(); ++iter)
  				{
  					FD_SET(iter->get_socket(), &fds);

  					if (iter->get_socket() > n) {
  						n = iter->get_socket();
  					}
  				}

  				struct timeval tv;
  				tv.tv_sec = 0;
  				tv.tv_usec = 100;	// Block for 100 usec on each connection
  				n++;
  				int rv = select(n, &fds, NULL, NULL, &tv);

  				if (rv > 0) {
  					for (set<Peer>::iterator iter = m_peers.begin();
  							iter != m_peers.end(); ++iter)
  					{
  						if (FD_ISSET(iter->get_socket(), &fds)) {
  							// If the remote end closed the connection, close
  							// this end's socket and delete the peer
  							if (connectionClosed(*iter)) {
  								ostringstream oss;
  								oss << "Peer " << ntohs(iter->get_port()) <<
  										" closed the connection.";
  								log(oss.str());

  								close(iter->get_socket());
  								m_peers.erase(iter);
  							}
  							else {
  								handleRequest(*iter);
  							}
  						}
  					}
  				}
  				*/
  			}
  		}
  	}

	// This function gets called when a node starts up, and wishes to connect
	// to the network.  It first needs to know an address and port of the
	// remote node that it wants to connect to.  This basically makes sure
	// that whoever we're going to be sending our first PING message to
	// is an actual gnutella node.
  	void bootstrap(const char *address, int port) {
  		int s = acquireSendSocket();
  		Peer peer(inet_addr(address), htons(port), s);

  		gnutellaConnect(peer);
  	}

  	void periodicPing() {
  		for (set<Peer>::iterator p = m_peers.begin(); p != m_peers.end(); p++)
  			sendPing(*p);
  	}

	// Send a PING to a given peer
	void sendPing(Peer peer) {
		MessageId id = generateMessageId();
		DescriptorHeader header(id, ping, DEFAULT_TTL, DEFAULT_HOPS, 0);
		sendToPeer(peer, &header, NULL);

		// Search for the target peer in the sent ping map
		map<Peer,map<MessageId,Peer> >::iterator x =
				m_sentPingMap.find(peer);

		// If there isn't a map from the message id to a peer already
		// already associated with the target peer, create one.
		if (x == m_sentPingMap.end()) {
			map<MessageId,Peer> idToPeer;
			idToPeer.insert(pair<MessageId,Peer>
					(header.get_message_id(), m_self));
			m_sentPingMap.insert(pair<Peer,map<MessageId,Peer> >
						(peer,idToPeer));
		}

		// There was a map, but the message id wasn't in the inner map,
		// add it.
		else {
			map<MessageId,Peer>::iterator y =
					x->second.find(header.get_message_id());
			if (y == x->second.end()) {
				// Clear out old PINGs
				while(x->second.size() >= MAX_PING_STORAGE)
					x->second.erase(x->second.begin());
				x->second.insert(pair<MessageId,Peer>
						(header.get_message_id(), m_self));
			}

			// There was a mapping from the id to a peer, but it wasn't right.
			// Fix that.
			else if (y->second == m_self) {
				// Do something?
			}
		}
	}

	// Send a QUERY to a given peer
	void sendQuery(Peer peer, string searchCriteria)
	{
		Query_Payload payload(m_minimumDownloadRate, searchCriteria);
		MessageId id = generateMessageId();
		DescriptorHeader header(id, query, DEFAULT_TTL,	DEFAULT_HOPS,
				payload.get_payload_len());

		sendToPeer(peer, &header, &payload);
	}

	// This function handles the user controlled
	void userNode()
	{
		string str;
		while (true) {
			cout << "5) Ping\n6) Query\n7) Accept Connections\n\
8) Exit to network.sh\n#? ";
			cin >> str;
			if (str == "5") {
				size_t i = 0;
				vector<Peer> peers_all, peers;
				for (set<Peer>::iterator p = m_peers.begin();
						p != m_peers.end(); p++) {
					in_addr addr;
					addr.s_addr = p->get_addr();
					cout << i+1 << ". " << inet_ntoa(addr) << " "
							<< ntohs(p->get_port()) << "\n";
					peers_all.push_back(*p);
					i++;
				}
				cout << "List all peers, separated by blank space, you wish to \
PING.\nFor all peers input \"all\"\n";
				while (true) {
					cout << "#? ";
					cin >> str;
					if (str.find("all") != string::npos) {
						peers = peers_all;
					}
					else {
						for (string::iterator it = str.begin();
								it != str.end(); it++) {
							char c = *it;
							size_t digit = c - '0';
							if (isdigit(c) && digit > 0 &&
									digit <= m_peers.size())
								peers.push_back(peers_all.at(digit-1));
						}
					}
					if (peers.size() == 0)
						cout << "Bad input\n";
					else
						break;
				}
				for (vector<Peer>::iterator it = peers.begin();
						it != peers.end(); it++)
					sendPing(*it);

				acceptConnections(PING_TIMEOUT);
			}
			else if (str == "6") {
				cout << "Please enter a filename to search for\n#? ";
				cin >> str;
				for (set<Peer>::iterator it = m_peers.begin();
										it != m_peers.end(); it++)
					sendQuery(*it, str);

				//acceptConnections(SENDQUERY_TIMEOUT);
			}
			else if (str == "7") {
				cout << "Please enter how many seconds to listen\n#? ";
				cin >> str;
				int timeout = atoi(str.c_str());
				if (timeout == 0) {
					acceptConnections(5);
				}
				else {
					acceptConnections(timeout);
				}
			}
			else if (str == "8")
				return;
			else
				cout << "Bad option\n";
		}
	}
};

int main(int argc, char **argv) {
  // Check if arguments passed
  Gnutella *node;
  if (argc >= 2) {
	node = new Gnutella(atoi(argv[1]));
  }
  else {
	node = new Gnutella();
  }

  if (argc >= 4) {
	node->bootstrap(argv[2], atoi(argv[3]));
  }

  if (argc >= 5 && strcmp(argv[4],"user") == 0) {
	  node->activateUserNode();
	  node->userNode();
	  delete node;
	  return 0;
  }

//  while (true) {
//	  node->periodicPing();
	  node->acceptConnections(PERIODIC_PING);
//	}
  delete node;

  return 0;
}
