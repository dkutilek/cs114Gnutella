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
#include "descriptor_header.h"
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
  unsigned long address;
  unsigned short port;
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
	int m_socket;		 // Holds the socket descriptor
	vector<peer_t> m_peers;  // A list of peers that this node knows about
	int m_port;		 // The port that this node will listen on, in little-endian format
	fstream m_log;
	unsigned char m_maximumUploadRate;		// in KB/s
	unsigned char m_minimumDownloadRate;	// in KB/s
	string m_sharedDirectoryName;
	vector<string> m_fileList;
  
	void error(string msg) {
		m_log << "[ERR " << get_time() << "] " << msg << ": " << strerror(errno) << endl;
		exit(1);
  	}
  
  	void log(string msg) {
		m_log << "[LOG " << get_time() << "] " << msg << endl;
  	}

	// Call this function to read the filenames in the directory
	// m_sharedDirectoryName into the vector m_fileList
	void readSharedDirectoryFiles() {
		DIR *dirp = opendir(m_sharedDirectoryName.c_str());
		
		if (dirp == NULL) {
			error("Could not open specified shared directory");
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
		}
	}
	
	// Call this function to acquire a socket in m_socket
	void acquireSocket() {
		m_socket = socket(PF_INET, SOCK_STREAM, 0);

		if (m_socket == -1) {
	  		error("Could not acquire socket");
		}
  	}

	// Call this function to bind the socket in m_socket to the port m_port
	void bindSocket() {
		sockaddr_in nodeInfo;
		memset(&nodeInfo, 0, sizeof(nodeInfo));
		nodeInfo.sin_family = AF_INET;
		nodeInfo.sin_addr.s_addr = INADDR_ANY;
		nodeInfo.sin_port = htons(m_port);
		
		int status = bind(m_socket, (sockaddr *) &nodeInfo, sizeof(nodeInfo));
		
		if (status == -1) {
			error("Could not bind to socket");
		}
	}
	
	// Call this function to listen for connections on the socket m_socket
	void listenOnSocket() {
		int status = listen(m_socket, 10000);
		
		if (status == -1) {
			error("Could not listen on socket");
		}
	}
	
	// Call this function to see if someone is trying to connect to us.
	// The return value is the connection handle.
	int getConnectionRequest(sockaddr_in *remoteInfo) {
		memset(remoteInfo, 0, sizeof(sockaddr_in));
		socklen_t addrLength;
		return accept(m_socket, (sockaddr *) remoteInfo, &addrLength);
	}
	
	// Use this function to read a descriptor header to a new string object.
	// Make sure to delete the string when you're done with it, since usually
	// this will be used inside a while loop.
	string *readDescriptorHeader(int connection) {
		char buffer[HEADER_SIZE];
		memset(buffer, 0, HEADER_SIZE);
		int used = 0;
		int remaining = HEADER_SIZE - 1;
		
		while (remaining > 0) {
			int bytesRead = recv(connection, &buffer[used], remaining, 0);
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
		
		return new string(buffer);
	}
	
	// This function will read the message payload based upon how large the
	// descriptor header said the payload is.  Make sure to delete the
	// returned string after you're done with it.
	string *readDescriptorPayload(int connection, int payloadSize) {
		if (payloadSize < 1) {
			error("Invalid payload size");
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
		
		return new string(buffer);
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
	
	// This function gets called when the node receives a "GNUTELLA CONNECT"
	// message.  It just sends a "GNUTELLA OK" response back on the same
	// connection.
	void handleConnect(int connection) {
		log("Received connect request.");
		char connectResponse[] = "GNUTELLA OK\n\n";
		send(connection, connectResponse, sizeof(connectResponse), 0);
	}
	
	// This function gets called when the node receives a QUERY message.
	void handleQuery(int connection, DescriptorHeader *header) {
		log("Received query request.");
		
		// Get the payload
		string *payload = readDescriptorPayload(connection, header->get_payload_len());
		
		// The first character of the payload should be the transfer rate
		unsigned char transferRate = payload->at(0);
		
		// The rest of the payload is a string that determines the file that the sender
		// is looking for
		string searchCriteria = payload->substr(1);
		
		delete payload;
	}

public:
  	Gnutella(int port = DEFAULT_PORT) {
		m_port = port;
		m_sharedDirectoryName = DEFAULT_SHARE_DIRECTORY;
		
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
  	}

  	~Gnutella() {
		m_log.close();
  	}

	// This function is called after the node has bootstrapped onto the 
	// network, discovered other nodes with PING,
	// and just wants to listen and respond to messages.
  	void acceptConnections() {
		acquireSocket();
		bindSocket();
		listenOnSocket();

		// Continuously accept connections
		while (true) {
			sockaddr_in remoteInfo;	// This structure holds info about whoever is connecting to us.
			int connection = getConnectionRequest(&remoteInfo);

	  		// Read the descriptor header
			string *messageHeader = readDescriptorHeader(connection);

	  		// Handle connect request
	  		if (strcmp(messageHeader->c_str(), "GNUTELLA CONNECT/0.4\n\n") == 0) {
				handleConnect(connection);
	  		}
			// Other requests
	  		else {
				// Construct a descriptor heaader from the message
				DescriptorHeader *header = new DescriptorHeader(messageHeader->c_str());
				
				switch (header->get_header_type()) {
					case ping:
						// Handle ping
					break;
					case pong:
						// Handle pong
					break;
					case query:
					handleQuery(connection, header);
					break;
					case queryHit:
						// Handle queryHit
					break;
					case push:
						// Handle push
					break;
				}
				
				delete(header);
	  		}

			delete messageHeader;
	  		close(connection);
		}

		// Close the socket
		close(m_socket);
  	}

	// This function gets called when a node starts up, and wishes to connect
	// to the network.  It first needs to know an address and port of the
	// remote node that it wants to connect to.  This basically makes sure
	// that whoever we're going to be sending our first PING message to
	// is an actual gnutella node.
  	void bootstrap(const char *address, int port) {
		acquireSocket();

		// Set up the structure for connecting to the remote node
		sockaddr_in nodeInfo;
		memset(&nodeInfo, 0, sizeof(nodeInfo));
		nodeInfo.sin_family = AF_INET;
		nodeInfo.sin_addr.s_addr = inet_addr(address);
		nodeInfo.sin_port = htons(port);

		// Try to start a connection
		int status = connect(m_socket, (sockaddr *) &nodeInfo, sizeof(nodeInfo));

		if (status == -1) {
	  		error("Could not connect to boostrap host");
		}

		char request[] = "GNUTELLA CONNECT/0.4\n\n";

		// Send a connect request
		log("Sending bootstrap connect request.");
		send(m_socket, request, sizeof(request), 0);

		// Get the response
		string *response = readDescriptorHeader(m_socket);
		
		// If the remote node replies correctly, we can use other messages,
		// otherwise, the node should fail.
		if (strcmp(response->c_str(), "GNUTELLA OK\n\n") == 0) {
			log("Connected to bootstrap host.");
		}
		else {
			log("The bootstrap host rejected the connect request.");
			close(m_socket);
			exit(1);
		}
		
		delete response;
		close(m_socket);	// Close the connection and free the socket
  	}

	// This function sends a query message to a remote node.
	void sendQuery(const char *address, unsigned short port, 
		string searchCriteria)
	{
		Query_Payload payload(m_minimumDownloadRate, searchCriteria);
		DescriptorHeader header(port, query, DEFAULT_TTL, DEFAULT_HOPS, payload.get_payload_len());
		
		// Setup socket/remote info
		acquireSocket();
		sockaddr_in remoteInfo;
		remoteInfoSetup(&remoteInfo, address, port);
		
		// Attempt a connection to the remote node
		int status = connect(m_socket, (sockaddr *) &remoteInfo, sizeof(remoteInfo));
		
		if (status == -1) {
			error("Could not connect to remote host");
		}
		
		// Send the message
		log("Sending QUERY message.");
		send(m_socket, header.get_header(), sizeof(header.get_header()), 0);
		send(m_socket, payload.get_payload(), sizeof(payload.get_payload()), 0);
		
		close(m_socket);	// Close the connection and free the socket	
	}

	// This function handles the user controlled
	void userNode()
	{
		string str;
		while (true) {
			cout << "5) Ping/Pong\n6) Query\n7) Exit to network.sh\n#? ";
			cin >> str;
			if (str == "5") {

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
	//node->sendQuery(argv[2], atoi(argv[3]), "test.txt");
  }

  if (argc >= 5 && strcmp(argv[4],"user") == 0) {
	  node->userNode();
	  delete node;
	  return 0;
  }

  node->acceptConnections();
  delete node;

  return 0;
}
