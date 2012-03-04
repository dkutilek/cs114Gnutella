#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <vector>
#include <cstdlib>
#include <string.h>
#include <string>
#include <fstream>
#include <ctime>
#include <climits>
#include "descriptor_header.h"
#include "payload.h"
#include <dirent.h>	// Directory reading

#define DEFAULT_PORT 11111
#define BUFFER_SIZE 1024
#define MAX_PEERS 7
#define DEFAULT_MAX_UPLOAD_RATE 10
#define DEFAULT_MIN_DOWNLOAD_RATE 10
#define DEFAULT_SHARE_DIRECTORY "./share"
#define DEFAULT_TTL 7
#define DEFAULT_HOPS 7

using namespace std;

// Contains an address and port in big-endian format
typedef struct peer {
  in_addr address;
  in_port_t port;
} peer_t;

string get_time() {
	time_t rawtime;
	struct tm * timeinfo;
	char buffer[80];
	time (&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, 80, "%m/%d/%y %H:%M:%S", timeinfo);

	return string(buffer);
}

class Gnutella {
private:
	int m_send, m_recv;
	vector<peer_t> m_peers;  // A list of peers that this node knows about
	int m_port;		 // The port that this node will listen on, in little-endian format
	fstream m_log;
	unsigned char m_maximumUploadRate;		// in KB/s
	unsigned char m_minimumDownloadRate;	// in KB/s
	string m_sharedDirectoryName;
	vector<string> m_fileList;
	unsigned int m_messageCount;
	bool m_userNode;
  
	void error(string msg) {
		m_log << "[ERR " << get_time() << "] " << msg << ": "
				<< strerror(errno) << endl;
		if (m_userNode)
			cerr << "[ERR " << get_time() << "] " << msg << ": "
			<< strerror(errno) << endl;
		//exit(1);
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
		}
		
		if (closedir(dirp)) {
			error("Could not close directory");
			exit(1);
		}
	}
	
	// Generate a unique message id using the port
	// and number of messages sent.
	unsigned long generateMessageId() {
		m_messageCount++;
		unsigned long id = m_messageCount;
		id <<= CHAR_BIT*2;
		id |= m_port;
		return id;
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
		nodeInfo.sin_port = htons(m_port);

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
		sockaddr_in nodeInfo;
		memset(&nodeInfo, 0, sizeof(nodeInfo));
		nodeInfo.sin_family = AF_INET;
		nodeInfo.sin_addr.s_addr = INADDR_ANY;
		nodeInfo.sin_port = htons(m_port+1);

		int status = bind(m_send, (sockaddr *) &nodeInfo, sizeof(nodeInfo));

		if (status == -1) {
			error("Could not bind to send socket");
			return -1;
		}
		return m_send;
	}

	// Close temporary send socket on port m_port+1.
	void closeSendSocket() {
		close(m_send);
	}

	// Call this function to see if someone is trying to connect to us.
	// The return value is the connection handle.
	int getConnectionRequest(sockaddr_in *remoteInfo) {
		memset(remoteInfo, 0, sizeof(sockaddr_in));
		socklen_t addrLength = sizeof (sockaddr);
		return accept(m_recv, (sockaddr *) remoteInfo, &addrLength);
	}
	
	// Use this function to read a descriptor header to a new descriptor header
	// object. Make sure to delete the header when you're done with it, since
	// usually this will be used inside a while loop.
	DescriptorHeader *readDescriptorHeader(int connection) {
		char buffer[HEADER_SIZE];
		memset(buffer, 0, HEADER_SIZE);
		int used = 0;
		int remaining = HEADER_SIZE - 1;
		
		while (remaining > 0) {
			int bytesRead = recv(connection, &buffer[used], remaining, 0);

			if (bytesRead < 0)
				return NULL;

			used += bytesRead;
			remaining -= bytesRead;
			buffer[used] = '\0';
			
			string str(buffer);
			int pos = str.find("\n\n"); 
			
			if (pos != -1) {
				memset(buffer, 0, sizeof(buffer));
				strcpy(buffer, str.substr(0, pos + 2).c_str());
				break;
			}
		}
		
		return new DescriptorHeader(buffer);
	}
	
	// This function will read the message payload based upon how large the
	// descriptor header said the payload is.  Make sure to delete the
	// returned payload after you're done with it.
	Payload *readDescriptorPayload(int connection, header_type type,
			int payloadSize) {
		if (payloadSize < 1) {
			error("Invalid payload size");
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

		switch (type) {
		case ping:
			return NULL;
		case pong:
			return new Pong_Payload(buffer);
		case query:
			return new Query_Payload(buffer, payloadSize);
		case queryHit:
			return new QueryHit_Payload(buffer, payloadSize);
		case push:
			return new Push_Payload(buffer);
		default:
			return NULL;
		}
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
	void remoteInfoSetup(sockaddr_in *remoteInfo, in_addr address,
		in_port_t port)
	{
		memset(remoteInfo, 0, sizeof(sockaddr_in));
		remoteInfo->sin_family = AF_INET;
		remoteInfo->sin_addr = address;
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
	void handlePing(int connection, DescriptorHeader *header) {
		// Do things with the PING
	}

	// This function gets called when the node recieves a PONG message.
	void handlePong(int connection, DescriptorHeader *header) {
		// Get the payload
		Pong_Payload *payload =(Pong_Payload *)
				readDescriptorPayload(connection, pong,
						header->get_payload_len());

		// Do things with the PONG

		delete payload;
	}

	// This function gets called when the node receives a QUERY message.
	void handleQuery(int connection, DescriptorHeader *header) {
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
	void handleQueryHit(int connection, DescriptorHeader *header) {
		// Get the payload
		QueryHit_Payload *payload = (QueryHit_Payload *)
				readDescriptorPayload(connection, queryHit,
						header->get_payload_len());

		// Do things with the QUERYHIT

		delete payload;
	}

	// This function gets called when the node receives a PUSH message.
	void handlePush(int connection, DescriptorHeader *header) {
		// Get the payload
		Push_Payload *payload = (Push_Payload *)
				readDescriptorPayload(connection, push,
						header->get_payload_len());

		// Do things with the PUSH

		delete payload;
	}

	void sendToAddrPort(in_addr addr, in_port_t port, DescriptorHeader* header,
			Payload* payload) {
		peer_t peer;
		peer.address = addr;
		peer.port = port;
		sendToPeer(peer, header, payload);
	}

	// Send a header and a payload to a given peer.  This will only work if the
	// node is not already actively listening on m_socket.  For CONNECT,
	// RESPONSE, and PING headers, payload = NULL.
	void sendToPeer(peer_t peer, DescriptorHeader* header, Payload* payload) {
		// Set up the structure for connecting to the remote node
		sockaddr_in nodeInfo;
		remoteInfoSetup(&nodeInfo, peer.address, peer.port);
		acquireSendSocket();

		// Try to start a connection
		int status = connect(m_send, (sockaddr *) &nodeInfo,
				sizeof(nodeInfo));
		if (status == -1) {
			ostringstream oss;
			oss << "Could not connect to peer at " << ntohs(peer.port);
	  		error(oss.str());
	  		closeSendSocket();
	  		return;
		}

		// Send a header followed by a payload
		ostringstream oss;
		oss << "Sending " << header->get_header_type() <<
				" request to peer at " << ntohs(peer.port);
		log(oss.str());
		status = send(m_send, header->get_header(), HEADER_SIZE, 0);
		if (status != HEADER_SIZE) {
			ostringstream logoss;
			logoss << "Failed to send " << header->get_header_type() <<
					" request to peer at " << ntohs(peer.port);
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
						<< " payload to peer at " << ntohs(peer.port);
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
		m_port = port;
		m_sharedDirectoryName = DEFAULT_SHARE_DIRECTORY;
		m_messageCount = 0;
		m_userNode = userNode;
		
		// Open the log
		char str[15];
		sprintf(str,"logs/log_%d",m_port);
		m_log.open(str,ios::out);
		
		if(!m_log.is_open()) {
	  		cerr << "Could not open log file: " << strerror(errno) << endl;
	  		exit(1);
		}
		
		// Get the list of filenames that this node is willing to share
		readSharedDirectoryFiles();

		acquireListenSocket();
  	}

  	~Gnutella() {
		m_log.close();
  	}

  	void activateUserNode() {
  		m_userNode = true;
  	}

	// This function is called after the node has bootstrapped onto the 
	// network, discovered other nodes with PING,
	// and just wants to listen and respond to messages.
  	void acceptConnections() {
		// Continuously accept connections
		while (true) {
			// This structure holds info about whoever is connecting to us.
			sockaddr_in remoteInfo;
			int connection = getConnectionRequest(&remoteInfo);

			// Construct a descriptor heaader from the message
			DescriptorHeader *header = readDescriptorHeader(connection);

			if (header == NULL)
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
					handlePing(connection, header);
					break;
				case pong:
					oss << "Received PONG from peer at "
						<< ntohs(remoteInfo.sin_port)-1;
					log(oss.str());
					handlePong(connection, header);
					break;
				case query:
					oss << "Received QUERY from peer at "
						<< ntohs(remoteInfo.sin_port)-1;
					log(oss.str());
					handleQuery(connection, header);
					break;
				case queryHit:
					oss << "Received QUERYHIT from peer at "
						<< ntohs(remoteInfo.sin_port)-1;
					log(oss.str());
					handleQueryHit(connection, header);
					break;
				case push:
					oss << "Received PUSH from peer at "
						<< ntohs(remoteInfo.sin_port)-1;
					log(oss.str());
					handlePush(connection, header);
					break;
				case resp:
					// Do nothing
					break;
			}

			close(connection);
			delete(header);
		}

		// Close the socket
		closeListenSocket();
  	}

	// This function gets called when a node starts up, and wishes to connect
	// to the network.  It first needs to know an address and port of the
	// remote node that it wants to connect to.  This basically makes sure
	// that whoever we're going to be sending our first PING message to
	// is an actual gnutella node.
  	void bootstrap(const char *address, int port) {
  		DescriptorHeader header(con);
  		peer_t peer;
  		peer.address.s_addr = inet_addr(address);
  		peer.port = htons(port);
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
			m_peers.push_back(peer);
		}
		else {
			log("The bootstrap host rejected the connect request.");
			exit(1);
		}
		
		delete response;
		close(connection);	// Close the connection and free the socket
  	}

	// Send a PING to a given peer
	void sendPing(peer_t peer) {
		DescriptorHeader header(generateMessageId(), ping, DEFAULT_TTL,
				DEFAULT_HOPS, 0);

		sendToPeer(peer, &header, NULL);
	}

	// Send a QUERY to a given peer
	void sendQuery(peer_t peer, string searchCriteria)
	{
		Query_Payload payload(m_minimumDownloadRate, searchCriteria);
		DescriptorHeader header(generateMessageId(), query, DEFAULT_TTL,
				DEFAULT_HOPS, payload.get_payload_len());

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
				for (size_t i = 0; i < m_peers.size(); i++) {
					peer_t p = m_peers.at(i);
					cout << i+1 << ". " << inet_ntoa(p.address) << " "
							<< ntohs(p.port) << "\n";
				}
				vector<peer_t> peers;
				cout << "List all peers, separated by blank space, you wish to \
						PING.\nFor all peers input \"all\"\n";
				while (true) {
					cout << "#? ";
					cin >> str;
					if (str.find("all") != string::npos) {
						peers = m_peers;
					}
					else {
						for (string::iterator it = str.begin();
								it != str.end(); it++) {
							char c = *it;
							size_t digit = c - '0';
							if (isdigit(c) && digit > 0 &&
									digit <= m_peers.size())
								peers.push_back(m_peers.at(digit-1));
						}
					}
					if (peers.size() == 0)
						cout << "Bad input\n";
					else
						break;
				}
				for (vector<peer_t>::iterator it = peers.begin();
						it != peers.end(); it++)
					sendPing(*it);
			}
			else if (str == "6") {

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
