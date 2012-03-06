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

#define DEFAULT_PORT 11111
#define BUFFER_SIZE 1024
#define MAX_PEERS 7
#define DEFAULT_MAX_UPLOAD_RATE 10
#define DEFAULT_MIN_DOWNLOAD_RATE 10
#define DEFAULT_SHARE_DIRECTORY "./share"
#define DEFAULT_TTL 7
#define DEFAULT_HOPS 7
#define PING_TIMEOUT 5
#define SENDQUERY_TIMEOUT 5

using namespace std;

class Gnutella {
private:
	int m_send, m_recv;
	set<Peer> m_peers;  // A set of peers that this node knows about
	Peer m_self;	// Contains the port and address of own node
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

	// Instantiate, bind, and listen on port m_port.
	void acquireListenSocket() {
		m_recv = socket(PF_INET, SOCK_STREAM, 0);
		if (m_recv == -1) {
			error("Could not acquire recv socket");
			exit(1);
		}

		sockaddr_in nodeInfo;
		memset(&nodeInfo, 0, sizeof(nodeInfo));
		nodeInfo.sin_family = AF_INET;
		nodeInfo.sin_addr.s_addr = INADDR_ANY;
		nodeInfo.sin_port = m_self.get_port();

		int status = bind(m_recv, (sockaddr *) &nodeInfo, sizeof(nodeInfo));
		if (status == -1) {
			error("Could not bind to recv socket");
			exit(1);
		}

		status = listen(m_recv, 10000);

		if (status == -1) {
			error("Could not listen on recv socket");
			exit(1);
		}
	}

	// Close the receiving socket on port m_port.
	void closeListenSocket() {
		close(m_recv);
	}

	// Open a temporary send socket on port m_port+1.  Close when done using
	// closeSendSocket().
	int acquireSendSocket() {
		m_send = socket(PF_INET, SOCK_STREAM, 0);
		if (m_send == -1) {
			error("Could not acquire send socket");
			return m_send;
		}

		int tr = 1;
		if (setsockopt(m_send,SOL_SOCKET,SO_REUSEADDR,&tr,sizeof(int)) == -1 ) {
			error("Could not set socket option");
			return -1;
		}

		sockaddr_in nodeInfo;
		memset(&nodeInfo, 0, sizeof(nodeInfo));
		nodeInfo.sin_family = AF_INET;
		nodeInfo.sin_addr.s_addr = m_self.get_addr();
		nodeInfo.sin_port = htons(ntohs(m_self.get_port())+1);

		int status = bind(m_send, (sockaddr *) &nodeInfo, sizeof(nodeInfo));

		if (status == -1) {
			ostringstream oss;
			oss << "Could not bind to send socket on port "
					<< ntohs(m_self.get_port())+1;
			error(oss.str());
			return -1;
		}
		return m_send;
	}

	// Close temporary send socket on port m_port+1.
	void closeSendSocket() {
		close(m_send);
	}

	// Call this function to see if someone is trying to connect to us.
	// The return value is the connection handle. The optional argument
	// timeout determines how long to wait for a connection. Passing 0 in
	// as the timeout means no timeout, it blocks indefinitely.
	int getConnectionRequest(sockaddr_in *remoteInfo, unsigned int timeout = 0) {
		memset(remoteInfo, 0, sizeof(sockaddr_in));
		socklen_t addrLength = sizeof (sockaddr);

		if (timeout != 0) {
			pollfd pfd;
			pfd.fd = m_recv;
			pfd.events = POLLIN;

			int retval = poll(&pfd, 1, timeout*100);

			if (retval == -1) {
				error("Poll on listening port failed");
				return -1;
			}
			else if (retval == 0)
				return -1;
			else
				return accept(m_recv, (sockaddr *) remoteInfo, &addrLength);
		}
		else
			return accept(m_recv, (sockaddr *) remoteInfo, &addrLength);
	}
	
	// Use this function to read a descriptor header to a new descriptor header
	// object. Make sure to delete the header when you're done with it, since
	// usually this will be used inside a while loop.
	DescriptorHeader *readDescriptorHeader(int connection) {
		char buffer[HEADER_SIZE];
		memset(buffer, 0, HEADER_SIZE);
		int used = 0;
		int remaining = HEADER_SIZE;
		
		while (remaining > 0) {
			int bytesRead = recv(connection, &buffer[used], remaining, 0);

			if (bytesRead < 0)
				return NULL;

			used += bytesRead;
			remaining -= bytesRead;
		}
		
		return new DescriptorHeader(buffer);
	}
	
	// This function will read the message payload based upon how large the
	// descriptor header said the payload is.  Make sure to delete the
	// returned payload after you're done with it.
	Payload *readDescriptorPayload(int connection, header_type type,
			int payloadSize) {
		if (payloadSize < 1) {
			log("Invalid payload size");
			return NULL;
		}
		
		char *buffer = new char[payloadSize];
		memset(buffer, 0, payloadSize);
		int used = 0;
		int remaining = payloadSize - 1;
		
		while (remaining > 0) {
			int bytesRead = recv(connection, &buffer[used], remaining, 0);
			used += bytesRead;
			remaining -= bytesRead;
			buffer[used] = '\0';
		}

		Payload * payload;

		switch (type) {
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
		delete buffer;
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

	// This function gets called when the node receives a "GNUTELLA CONNECT"
	// message.  It just sends a "GNUTELLA OK" response back.
	void handleConnect(in_addr address, in_port_t port) {
		DescriptorHeader header(resp);
		unsigned short p = ntohs(port);
		sendToAddrPort(address, htons(p-1), &header, NULL);
	}

	// This function gets called when the node receives a PING message.
	void handlePing(int connection, DescriptorHeader *header,
			in_addr address, in_port_t port) {
		bool sent = false;
		Peer peer(address.s_addr, htons(ntohs(port)-1));

		DescriptorHeader d(header->get_message_id(),
				header->get_header_type(), header->get_time_to_live()-1,
				header->get_hops()+1, header->get_payload_len());

		// Pass PING along to all our peers
		for (set<Peer>::iterator it = m_peers.begin();
				it != m_peers.end(); it++) {
			if ((*it) == peer) {
				sendPong(*it, header->get_message_id());
				sent = true;
			}
			else {
				map<Peer,map<MessageId,Peer> >::iterator x =
						m_sentPingMap.find(*it);

				// If there isn't a map from the message id to a peer already
				// associated with this peer, forward the PING.
				if (x != m_sentPingMap.end()) {
					map<MessageId,Peer>::iterator y =
							x->second.find(header->get_message_id());
					if (y == x->second.end()) {
						sendToPeer(*it, &d, NULL);
						x->second.insert(pair<MessageId,Peer>
								(header->get_message_id(),*it));
					}
				}

				// There wasn't a map for this peer at all, forward the PING
				else {
					sendToPeer(*it, &d, NULL);
					map<MessageId,Peer> idToPeer;
					idToPeer.insert(pair<MessageId,Peer>
							(header->get_message_id(),peer));
					m_sentPingMap.insert(pair<Peer,map<MessageId,Peer> >
							(*it,idToPeer));
				}
			}
		}

		// Add to our list of peers if we can
		set<Peer>::iterator setIter = m_peers.find(peer);
		if (!sent && setIter == m_peers.end() && m_peers.size() < MAX_PEERS) {
			m_peers.insert(peer);
			sendPong(peer, header->get_message_id());
		}
	}

	// This function gets called when the node receives a PONG message.
	void handlePong(int connection, DescriptorHeader *header,
			in_addr address, in_port_t port) {
		// Get the payload
		Pong_Payload *payload =(Pong_Payload *)
				readDescriptorPayload(connection, pong,
						header->get_payload_len());

		Peer peer(address.s_addr, htons(ntohs(port)-1));

		// Check if we need to pass this PONG along
		map<Peer,map<MessageId,Peer> >::iterator x =
				m_sentPingMap.find(peer);
		if (x != m_sentPingMap.end()) {
			map<MessageId,Peer>::iterator y =
					x->second.find(header->get_message_id());
			if (y != x->second.end()) {

				// We might be the actual target
				if (y->second == m_self) {
					Peer new_peer(payload->get_ip_addr(), payload->get_port(),
							payload->get_files_shared(),
							payload->get_kilo_shared());
					m_peers.insert(new_peer);
					ostringstream oss;
					oss << "New peer at " << ntohs(payload->get_port())
							<< " added";
					log(oss.str());
				}
				// If not send it along
				else {
					DescriptorHeader d(header->get_message_id(),
							header->get_header_type(), header->get_time_to_live()-1,
							header->get_hops()+1, header->get_payload_len());
					sendToPeer(y->second, &d, payload);
				}

				// Erase saved PING
				x->second.erase(y->first);
			}
		}
		delete payload;
	}

	// This function gets called when the node receives a QUERY message.
	void handleQuery(int connection, DescriptorHeader *header,
			in_addr address, in_port_t port) {
		// Get the payload
		Query_Payload *payload = (Query_Payload *)
				readDescriptorPayload(connection, query,
						header->get_payload_len());

		// The first character of the payload should be the transfer rate
		//unsigned short transferRate = payload->get_speed();

		// The rest of the payload is a string that determines the file that
		// the sender is looking for
		//string searchCriteria = payload->get_search();

		delete payload;
	}

	// This function gets called when the node receives a QUERYHIT message.
	void handleQueryHit(int connection, DescriptorHeader *header,
			in_addr address, in_port_t port) {
		// Get the payload
		QueryHit_Payload *payload = (QueryHit_Payload *)
				readDescriptorPayload(connection, queryHit,
						header->get_payload_len());

		// Do things with the QUERYHIT

		delete payload;
	}

	// This function gets called when the node receives a PUSH message.
	void handlePush(int connection, DescriptorHeader *header,
			in_addr address, in_port_t port) {
		// Get the payload
		Push_Payload *payload = (Push_Payload *)
				readDescriptorPayload(connection, push,
						header->get_payload_len());

		// Do things with the PUSH

		delete payload;
	}

	// Send a PONG to a given peer
	void sendPong(Peer peer, MessageId& messageId) {
		DescriptorHeader header(messageId, pong, DEFAULT_TTL,
								DEFAULT_HOPS, PONG_LEN);
		Pong_Payload payload(m_self.get_port(), m_self.get_addr(),
				m_fileList.size(), m_kilobyteCount);

		sendToPeer(peer, &header, &payload);
	}

	void sendToAddrPort(in_addr addr, in_port_t port, DescriptorHeader* header,
			Payload* payload) {
		Peer peer(addr.s_addr, port);
		sendToPeer(peer, header, payload);
	}

	// Send a header and a payload to a given peer.  This will only work if the
	// node is not already actively listening on m_socket.  For CONNECT,
	// RESPONSE, and PING headers, payload = NULL.
	void sendToPeer(Peer peer, DescriptorHeader* header, Payload* payload) {
		// Set up the structure for connecting to the remote node
		sockaddr_in nodeInfo;
		remoteInfoSetup(&nodeInfo, peer.get_addr(), peer.get_port());
		if (acquireSendSocket() == -1) {
			ostringstream oss;
			oss << "Could not connect to peer at " << ntohs(peer.get_port());
			log(oss.str());
			closeSendSocket();
			return;
		}

		// Try to start a connection
		int status = connect(m_send, (sockaddr *) &nodeInfo,
				sizeof(nodeInfo));
		if (status == -1) {
			ostringstream oss;
			oss << "Could not connect to peer at " << ntohs(peer.get_port());
	  		error(oss.str());
	  		closeSendSocket();
	  		return;
		}

		// Send a header followed by a payload
		ostringstream oss;
		oss << "Sending " << header->get_header_type() <<
				" request to peer at " << ntohs(peer.get_port());
		log(oss.str());
		status = send(m_send, header->get_header(), HEADER_SIZE, 0);
		if (status != HEADER_SIZE) {
			ostringstream logoss;
			logoss << "Failed to send " << header->get_header_type() <<
					" request to peer at " << ntohs(peer.get_port());
			error(logoss.str());
			closeSendSocket();
			return;
		}
		if (payload != NULL) {
			status = send(m_send, payload->get_payload(),
					payload->get_payload_len(), 0);
			if (status < 0 ||
					(unsigned int) status != payload->get_payload_len()) {
				ostringstream logoss;
				logoss << "Failed to send " << header->get_header_type()
						<< " payload to peer at " << ntohs(peer.get_port());
				error(logoss.str());
				closeSendSocket();
				return;
			}
		}
		closeSendSocket();
	}

	// Send a header and a payload over an already opened connection.
	void sendOverConnection(int connection, DescriptorHeader* header,
			Payload* payload) {
		int status = send(connection, header->get_header(), HEADER_SIZE, 0);
		if (status == -1) {
			ostringstream logoss;
			logoss << "Failed to send " << header->get_header_type()
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
				logoss << "Failed to send " << header->get_header_type()
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

		m_self = Peer(inet_addr("127.0.0.1"), htons(port),
				(unsigned long) m_fileList.size(), m_kilobyteCount);

		acquireListenSocket();
  	}

  	~Gnutella() {
		m_log.close();
		closeListenSocket();
  	}

  	void activateUserNode() {
  		m_userNode = true;
  	}

	// This function is called after the node has bootstrapped onto the 
	// network, discovered other nodes with PING, and just wants to listen
  	// and respond to messages. The optional argument timeout will cause this
  	// function to return after timeout seconds.
  	void acceptConnections(unsigned int timeout = 0) {
  		time_t starttime = time(NULL);
		// Continuously accept connections while timeout is greater than
  		// current time - start time.
		while (timeout == 0 || difftime(time(NULL), starttime) < timeout) {
			// This structure holds info about whoever is connecting to us.
			sockaddr_in remoteInfo;
			int connection = getConnectionRequest(&remoteInfo, timeout);

			// Construct a descriptor heaader from the message
			DescriptorHeader *header = readDescriptorHeader(connection);

			if (header == NULL)
				continue;

			if (header->get_header_type() != con &&
					header->get_time_to_live() == 0)
				continue;

			ostringstream oss;
			// Handle requests
			switch (header->get_header_type()) {
				case con:
					oss << "Received CONNECT from peer at "
						<< ntohs(remoteInfo.sin_port)-1;
					log(oss.str());
					handleConnect(remoteInfo.sin_addr, remoteInfo.sin_port);
					break;
				case ping:
					oss << "Received PING from peer at "
						<< ntohs(remoteInfo.sin_port)-1;
					log(oss.str());
					handlePing(connection, header,
							remoteInfo.sin_addr, remoteInfo.sin_port);
					break;
				case pong:
					oss << "Received PONG from peer at "
						<< ntohs(remoteInfo.sin_port)-1;
					log(oss.str());
					handlePong(connection, header,
							remoteInfo.sin_addr, remoteInfo.sin_port);
					break;
				case query:
					oss << "Received QUERY from peer at "
						<< ntohs(remoteInfo.sin_port)-1;
					log(oss.str());
					handleQuery(connection, header,
							remoteInfo.sin_addr, remoteInfo.sin_port);
					break;
				case queryHit:
					oss << "Received QUERYHIT from peer at "
						<< ntohs(remoteInfo.sin_port)-1;
					log(oss.str());
					handleQueryHit(connection, header,
							remoteInfo.sin_addr, remoteInfo.sin_port);
					break;
				case push:
					oss << "Received PUSH from peer at "
						<< ntohs(remoteInfo.sin_port)-1;
					log(oss.str());
					handlePush(connection, header,
							remoteInfo.sin_addr, remoteInfo.sin_port);
					break;
				case resp:
					// Do nothing
					break;
			}

			close(connection);
			delete header;
		}
  	}

	// This function gets called when a node starts up, and wishes to connect
	// to the network.  It first needs to know an address and port of the
	// remote node that it wants to connect to.  This basically makes sure
	// that whoever we're going to be sending our first PING message to
	// is an actual gnutella node.
  	void bootstrap(const char *address, int port) {
  		DescriptorHeader header(con);
  		Peer peer(inet_addr(address), htons(port));
  		sendToPeer(peer, &header, NULL);

  		// Accept the connection
  		sockaddr_in nodeInfo;
  		int connection = getConnectionRequest(&nodeInfo);

		// Get the response
		DescriptorHeader *response = readDescriptorHeader(connection);
		
		// If the remote node replies correctly, we can use other messages,
		// otherwise, the node should fail.
		if (response != NULL && response->get_header_type() == resp) {
			log("Connected to bootstrap host.");
			// Add to peer list.
			m_peers.insert(peer);
		}
		else {
			log("The bootstrap host rejected the connect request.");
			exit(1);
		}
		
		delete response;
		close(connection);	// Close the connection and free the socket
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
			cout << "5) Ping\n6) Query\n7) Exit to network.sh\n#? ";
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
				cout << "Please enter a filename to search for\n$? ";
				cin >> str;
				for (set<Peer>::iterator it = m_peers.begin();
										it != m_peers.end(); it++)
					sendQuery(*it, str);

				acceptConnections(SENDQUERY_TIMEOUT);
			}
			else if (str == "7")
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

  node->acceptConnections();
  delete node;

  return 0;
}
