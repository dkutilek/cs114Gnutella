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
#include <sys/ioctl.h>
#include <getopt.h>

#define DEFAULT_PORT 11111
#define BUFFER_SIZE 1024
#define MAX_PEERS 3
#define MAX_PING_STORAGE 32
#define MAX_QUERY_STORAGE 32
#define MAX_QUERY_HIT_STORAGE 32
#define DEFAULT_MAX_UPLOAD_RATE 10
#define DEFAULT_MIN_DOWNLOAD_RATE 10
#define DEFAULT_SHARE_DIRECTORY "./share"
#define DEFAULT_TTL 3
#define DEFAULT_HOPS 0
#define PING_TIMEOUT 5
#define SENDQUERY_TIMEOUT 5
#define CONNECT_TIMEOUT 5
#define HTTP_TIMEOUT 5
#define PERIODIC_PING 60
#define BOOT_WAIT 20

using namespace std;

class Gnutella {
private:
	set<Peer> m_peers;  	// A set of peers that this node knows about.
	Peer m_self;			// Contains information about this node.
	fstream m_log;
	unsigned char m_maximumUploadRate;		// in KB/s
	unsigned char m_minimumDownloadRate;	// in KB/s
	string m_sharedDirectoryName;
	vector<SharedFile> m_fileList;
	vector<QueryHit_Payload> m_queryHits;
	unsigned long m_messageCount;
	bool m_userNode;
	bool m_superNode;
	bool m_clientNode;
	string m_searching_for_file_name;

	// Map from a peer that we forwarded a PING to, to a map from a descriptor
	// ID to a peer who forwarded us the PING. Built when handling PING, used
	// when handling PONG.
	map<Peer,map<MessageId,Peer> > m_sentPingMap;

	// Map from a peer that we forwarded a QUERY to, to a map from a descriptor
	// ID to a peer who forwarded us the QUERY.  Built when handling QUERY,
	// used when handling QUERYHIT.
	map<Peer,map<MessageId,Peer> > m_sentQueryMap;
  
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

		unsigned long kilobyteCount = 0;
		m_fileList.clear();
		
		DIR *dirp = opendir(m_sharedDirectoryName.c_str());
		
		if (dirp == NULL) {
			int status = mkdir(m_sharedDirectoryName.c_str(), 0777);

			if (status == -1) {
				error("Could not create specified shared directory");
				exit(1);
			}

			dirp = opendir(m_sharedDirectoryName.c_str());
		}
		
		while (true) {
			struct dirent *entry;
			entry = readdir(dirp);
			
			if (entry == NULL) {
				break;
			}

			string filename(entry->d_name);

			if (filename == "." || filename == "..") {
				continue;
			}

			string path = m_sharedDirectoryName + "/" + filename;

			//read the file into a buffer
			FILE * p_File;
			long l_Size = 0;

			p_File = fopen ( path.c_str() , "r" );
			if (p_File==NULL) {fputs ("File error",stderr); exit (1);}

			// obtain file size:
			fseek (p_File , 0 , SEEK_END);
			l_Size = ftell (p_File);
			rewind (p_File);
			
			
			if (l_Size == 0) {
				ostringstream oss;
				oss << "Failed to get file size of " << filename;
				error(oss.str());
			}
			else {
				kilobyteCount += (l_Size * 1000);
				SharedFile file(filename, l_Size);
				m_fileList.push_back(file);
			}
		}
		
		if (closedir(dirp)) {
			error("Could not close directory");
			exit(1);
		}
		
		m_self.set_numSharedKilobytes(kilobyteCount);
		m_self.set_numSharedFiles(m_fileList.size());
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

		// Add non-blocking flag.
		int x;
		x = fcntl(sock, F_GETFL, 0);
		fcntl(sock, F_SETFL, x | O_NONBLOCK);

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

		time_t starttime = time(NULL);

		int result = -1;
		while (timeout == 0 || difftime(time(NULL), starttime) < timeout) {
			result = accept(m_self.get_recv(), (sockaddr *) remoteInfo,
			 			&addrLength);
			if (result != -1)
				break;
		}

		return result;
	}
	
	/**
	 * This function reads a descriptor header from a peer connection, and
	 * returns a new DescriptorHeader object.  Make sure to delete it when
	 * you are done with it.
	 */
	DescriptorHeader *readDescriptorHeader(Peer peer) {
		char buffer[HEADER_SIZE+1];
		memset(buffer, 0, HEADER_SIZE);
		int used = 0;
		int remaining = HEADER_SIZE;

		while (remaining > 0) {

			int bytesRead = recv(peer.get_recv(), &buffer[used], remaining, 0);

			if (bytesRead < 0) {
				ostringstream oss;
				oss << "Error while receiving from peer at "
						<< ntohs(peer.get_port());
				error(oss.str());
				return NULL;
			}

			used += bytesRead;
			remaining -= bytesRead;
		}
		buffer[HEADER_SIZE+1] = 0;
		
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
		
		int bytesAvailable;
		if (ioctl(peer.get_recv(), FIONREAD, &bytesAvailable) == 0) {
			if (bytesAvailable < payloadSize) {
				ostringstream oss;
				oss << "bytesAvailable = " << bytesAvailable << "\n" <<
						"payloadSize = " << payloadSize << "\n";
				error(oss.str());
				return NULL;
			}
		}
		else {
			error("ioctl failed in payload");
			ostringstream oss;
			oss << "Peer " << ntohs(peer.get_port()) <<
					" closed the connection.";
			log(oss.str());

			close(peer.get_recv());
			close(peer.get_send());
			m_peers.erase(peer);
			return NULL;
		}

		char *buffer = new char[payloadSize+1];
		memset(buffer, 0, payloadSize);
		int used = 0;
		int remaining = payloadSize;
		
		while (remaining > 0) {

			int bytesRead = recv(peer.get_recv(), &buffer[used], remaining, 0);

			if (bytesRead < 0) {
				ostringstream oss;
				oss << "Error while receiving "
						<< type_to_str(header.get_header_type())
						<< " from peer at "	<< ntohs(peer.get_port());
				error(oss.str());
				delete[] buffer;
				return NULL;
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
		case httpget:
			payload = new HTTPget_Payload(buffer, payloadSize);
			break;
		case httpok:
			payload = new HTTPok_Payload(buffer, payloadSize);
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

					int sock = acquireSendSocket();
					Peer new_peer(payload->get_ip_addr(), payload->get_port(),
							sock, -1, payload->get_files_shared(),
							payload->get_kilo_shared());

					set<Peer>::iterator iter = m_peers.find(new_peer);

					// Connect to the peer if we don't already know about it
					// and we can accept more peers.
					if (new_peer != m_self && iter == m_peers.end()
							&& m_peers.size() < MAX_PEERS) {
						gnutellaConnect(&new_peer, CONNECT_TIMEOUT);
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
				}
			}
		}
		delete payload;
	}

	// This function gets called when the node receives a QUERY message.
	void handleQuery(DescriptorHeader *header, Peer peer) {
		DescriptorHeader d(header->get_message_id(),
				header->get_header_type(), header->get_time_to_live()-1,
				header->get_hops()+1, header->get_payload_len());

		Query_Payload *payload =
				(Query_Payload *) readDescriptorPayload(peer, *header);

		if (payload == NULL) {
			return;
		}

		// Pass QUERY along to all our peers
		for (set<Peer>::iterator it = m_peers.begin(); it != m_peers.end();
					it++)
		{
			// Ignore whoever just sent us the query.
			if ((*it) == peer) {
				continue;
			}

			map<Peer,map<MessageId,Peer> >::iterator x =
					m_sentQueryMap.find(*it);
			// If there isn't a map from the message id to a peer already
			// associated with this peer
			if (x != m_sentQueryMap.end()) {
				map<MessageId,Peer>::iterator y =
								x->second.find(header->get_message_id());

				if (y == x->second.end()) {
					// Clear out old QUERYs
					while(x->second.size() >= MAX_QUERY_STORAGE) {
						x->second.erase(x->second.begin());
					}

					x->second.insert(pair<MessageId,Peer>
							(header->get_message_id(),peer));
					// Forward the QUERY
					sendToPeer(*it, &d, payload);
				}
			}
			// There wasn't a map for this peer at all
			else {
				map<MessageId,Peer> idToPeer;
				idToPeer.insert(pair<MessageId,Peer>
						(header->get_message_id(),peer));
				m_sentQueryMap.insert(pair<Peer,map<MessageId,Peer> >
								(*it,idToPeer));
				// Forward the QUERY
				sendToPeer(*it, &d, payload);
			}
		}

		unsigned short requestedTransferRate = payload->get_speed();
		string searchCriteria = payload->get_search();
		vector<Result> hits;

		// Check if this node has a file that matches the search criteria
		for (vector<SharedFile>::iterator file = m_fileList.begin();
				file != m_fileList.end(); file++)
		{
			if (file->getFileName() == searchCriteria) {
				Result result(file->getFileIndex(), file->getBytes(),
						file->getFileName());
				hits.push_back(result);
			}
		}

		// Build a QUERYHIT message if there are any hits, and this node
		// can support the requested transfer rate
		if (requestedTransferRate <= m_maximumUploadRate &&
				hits.size() > 0)
		{
			sendQueryHit(peer, header->get_message_id(), hits);
		}

		delete payload;
	}

	/*
	// This function gets called when the node receives a QUERY message.
	void handleQuery(DescriptorHeader *header, Peer peer) {
		// Get the payload
		Query_Payload *payload = (Query_Payload *)
				readDescriptorPayload(peer, *header);

		if (payload == NULL)
 			return;

		// The first character of the payload should be the transfer rate
		unsigned short transferRate = payload->get_speed();

		// The rest of the payload is a string that determines the file
		// that the sender is looking for.
		string searchCriteria = payload->get_search();
		vector<Result> hits;

		// Check if this node has a file that matches the search criteria
		for (vector<SharedFile>::iterator file = m_fileList.begin();
				file != m_fileList.end(); file++)
		{
			if (file->getFileName() == searchCriteria) {
				Result result(file->getFileIndex(), file->getBytes(),
						file->getFileName());
				hits.push_back(result);
			}
		}

		// Send the QUERYHIT if there are any hits, and the upload rate
		// of the local node is greater than the minimum transfer rate requested
		if (transferRate <= m_maximumUploadRate && hits.size() > 0) {
			sendQueryHit(peer, header->get_message_id(), hits);
		}
		// Otherwise pass along to all our peers
		else {
			for (set<Peer>::iterator iter = m_peers.begin();
					iter != m_peers.end(); iter++)
			{
				if (*(iter) != peer) {
					DescriptorHeader newHeader(header->get_message_id(),
							query, header->get_time_to_live() - 1,
							header->get_hops() + 1, payload->get_payload_len());

					map<Peer, map<MessageId, Peer> >::iterator x =
							m_sentQueryMap.find(*iter);

					if (x != m_sentQueryMap.end()) {
						map<MessageId,Peer>::iterator y =
								x->second.find(header->get_message_id());
						if (y == x->second.end()) {
							// Clear out old QUERYs
							while(x->second.size() >= MAX_QUERY_STORAGE) {
								x->second.erase(x->second.begin());
							}

							x->second.insert(pair<MessageId,Peer>
									(header->get_message_id(),peer));
							// Forward the QUERY
							sendToPeer(*iter, &newHeader, payload);
						}
					}
				}
			}
		}

		delete payload;
	}
	*/

	/**
	 * This function gets called when the node receives a QUERYHIT message.
	 * It checks the sent query map to see if it is the actual target or if
	 * it needs to forward the message to another peer.
	 */
	void handleQueryHit(DescriptorHeader *header, Peer peer) {
		// Get the payload
		QueryHit_Payload *payload = (QueryHit_Payload *)
				readDescriptorPayload(peer, *header);

		if (payload == NULL)
			return;

		// Check if we need to pass this QUERYHIT along
		map<Peer, map<MessageId, Peer> >::iterator x =
				m_sentQueryMap.begin();

		if (x != m_sentQueryMap.end()) {
			map<MessageId, Peer>::iterator y =
					x->second.find(header->get_message_id());

			if (y != x->second.end()) {
				// We might be the actual target
				if (y->second == m_self) {
					// Clear out old values
					while(m_queryHits.size() > MAX_QUERY_STORAGE) {
						m_queryHits.erase(m_queryHits.begin());
					}
					// Add the QUERYHIT to the QUERYHIT vector
					m_queryHits.push_back(*payload);

					//QueryHit needs to prompt user to ask to do
					// direct download (call sendHTTPget) OR
					// Push along same path
				}
				// If not send it along
				else {
					DescriptorHeader d(header->get_message_id(),
							header->get_header_type(),
							header->get_time_to_live() - 1,
							header->get_hops() + 1,
							header->get_payload_len());
					sendToPeer(y->second, &d, payload);
				}
			}
		}

		delete payload;
	}

	//pretty much parse and call sendHTTPok in response
	void handleHTTPget(int connection, DescriptorHeader *header, Peer peer)
	{
		// Get the payload
		HTTPget_Payload *payload = (HTTPget_Payload *)
				readDescriptorPayload(peer, *header);

		string request = payload->get_request();
		unsigned int slash_position = request.find('/', 9);
		string s_file_index = request.substr(9, slash_position - 9);
		unsigned int file_index = atoi(s_file_index.c_str());

		sendHTTPok(connection, file_index);
		
	}

	//parse the message and create a buffer[file_size] into which we'll read the file
	void handleHTTPok(int connection, DescriptorHeader *header, Peer peer)
	{
		// Get the payload
		HTTPok_Payload *payload = (HTTPok_Payload *)
				readDescriptorPayload(peer, *header);
				
		string response = payload->get_response();
		int s_size = response.find('\r', 80) - 81;
		string s_file_size = response.substr(81, s_size);

		int file_size = atoi(s_file_size.c_str());

		char * buffer;
		buffer = (char*) malloc (file_size);
		if (buffer == NULL) {fputs ("Memory error",stderr); exit (2);}
		
		read(connection, buffer, file_size);

		string path = m_sharedDirectoryName + "/" + m_searching_for_file_name;

		//create new file 
		ofstream outfile (path.c_str());
		FILE * pFile2;
		pFile2 = fopen(path.c_str(), "wb");
		fwrite (buffer , 1 , file_size , pFile2);

		//re-read shared files and add our new file
		readSharedDirectoryFiles();

		close(connection);
		
		
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
	void gnutellaConnect(Peer * peer, int timeout = 0) {
	  	// Try to connect through TCP to peer.
	  	while (!connectToPeer(*peer)) {}

	  	// Send a connect message
		DescriptorHeader request(m_self.get_port(), m_self.get_addr());
		sendToPeer(*peer, &request, NULL);

		sockaddr_in remoteInfo;
		int connection = getConnectionRequest(&remoteInfo, timeout);
		if (connection == -1) {
			error("Timed out while waiting on gnutella connect response");
			return;
		}

		peer->set_recv(connection);

		// Get the response
		DescriptorHeader *response;
		response = readDescriptorHeader(*peer);

		// If it returned null, try again
		while (response == NULL) {
			response = readDescriptorHeader(*peer);
		}

		// If it's the correct response, try to add to our list of peers
		if (response != NULL && response->get_header_type() == resp) {
			log("Connected to peer.");

			set<Peer>::iterator iter = m_peers.find(*peer);

			// Make sure the peer isn't already known, and we have room
			// for it
			if (iter == m_peers.end() && m_peers.size() < MAX_PEERS) {
				ostringstream oss;
				oss << "New peer at " << ntohs(peer->get_port()) << " added";
				log(oss.str());

				m_peers.insert(*peer);
			}
			else {
				close(connection);
				close(peer->get_send());
			}
		}
		else {
			log("Peer rejected CONNECT request.");
			close(connection);
			close(peer->get_send());
		}

		delete response;
	}

	/**
	 * Send a QUERYHIT to a given peer
	 */
	void sendQueryHit(Peer peer, MessageId &messageId,
			vector<Result> resultSet)
	{
		string serventId = m_self.getServentID();

		QueryHit_Payload payload(m_self.get_port(), m_self.get_addr(),
				m_maximumUploadRate, resultSet, serventId.c_str());

		DescriptorHeader header(messageId, queryHit, DEFAULT_TTL, DEFAULT_HOPS,
				payload.get_payload_len());

		sendToPeer(peer, &header, &payload);
	}

	// Send a PONG to a given peer
	void sendPong(Peer peer, MessageId& messageId) {
		DescriptorHeader header(messageId, pong, DEFAULT_TTL,
								DEFAULT_HOPS, PONG_LEN);
		Pong_Payload payload(m_self.get_port(), m_self.get_addr(),
				m_self.get_numSharedFiles(), m_self.get_numSharedKilobytes());

		sendToPeer(peer, &header, &payload);
	}

	// Send a HTTPget request to <ip_addr,port>
	void sendHTTPget(in_port_t port, in_addr_t ip_addr, vector<Result> resultSet) 
	{

		if(resultSet.size() == 0)
			error("Empty result set, query did not find file.");

		//currently just choose first result from set
		unsigned long file_index = resultSet[0].get_file_index();
		unsigned long file_size = resultSet[0].get_file_size();
		string file_name = resultSet[0].get_file_name();

		//record name of file we are searching for to save
		// as same file_name when received. 	
		m_searching_for_file_name = file_name;
		
		HTTPget_Payload payload(file_index, file_size, file_name);
		MessageId id = generateMessageId();
		DescriptorHeader header(id, httpget, DEFAULT_TTL, DEFAULT_HOPS, payload.get_payload_len());

		int sock = acquireSendSocket();
		//Peer new_peer(ip_addr, port, sock, -1);

		sockaddr_in nodeInfo;
		remoteInfoSetup(&nodeInfo, ip_addr, port);
		
		int status;
		time_t starttime = time(NULL);
		while ((status = connect(sock, (sockaddr *) &nodeInfo,
				sizeof(nodeInfo))) == -1 &&
				difftime(time(NULL), starttime) < CONNECT_TIMEOUT
				&& errno == EADDRINUSE){};
		if (status == -1) {
			ostringstream oss;
			oss << "Could not connect to peer at " << ntohs(port);
			error(oss.str());
		}
		sendOverConnection(sock, &header, &payload);
							
	}

	// Send a HTTPok response along same connection that received the HTTPget request
	void sendHTTPok(int connection, unsigned long file_index)
	{
		unsigned int file_index_int = file_index;
		int file_size;
		string file_name;
		// Check if this node has a file that matches the file index requested
		for (vector<SharedFile>::iterator file = m_fileList.begin();
				file != m_fileList.end(); file++)
		{
			if (file->getFileIndex() == file_index_int) {
				file_size = file->getBytes();
				file_name = file->getFileName();
			}
		}
	
		HTTPok_Payload payload(file_size);
		MessageId id = generateMessageId();
		DescriptorHeader header(id, httpok, DEFAULT_TTL, DEFAULT_HOPS, payload.get_payload_len());

		//send the ok message back to the node that sent the get message
		sendOverConnection(connection, &header, &payload);
		
		//get path of file	
		string path = m_sharedDirectoryName + "/" + file_name;

		//read the file into a buffer
		FILE * pFile;
	    long lSize;
	    char * buffer;
	    size_t result;

	    pFile = fopen ( path.c_str() , "r" );
	    if (pFile==NULL) {fputs ("File error",stderr); exit (1);}

	    // obtain file size:
	    fseek (pFile , 0 , SEEK_END);
	    lSize = ftell (pFile);
	    rewind (pFile);

	    // allocate memory to contain the whole file:
	    buffer = (char*) malloc (sizeof(char)*lSize);
	    if (buffer == NULL) {fputs ("Memory error",stderr); exit (2);}

	    // copy the file into the buffer:
	    result = fread (buffer,1,lSize,pFile);
	    if (result != lSize) {fputs ("Reading error",stderr); exit (3);}

	    /* the whole file is now loaded in the memory buffer. */

	    fclose (pFile);

		//write the file to the socket 
	    write(connection, buffer, result);

	    free(buffer);

		
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
		while ((status = connect(peer.get_send(), (sockaddr *) &nodeInfo,
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

		// Consolidate the header and payload into one buffer
		long len = HEADER_SIZE;
		if (payload != NULL)
			len+= header->get_payload_len();
		char * buff = new char[len+1];
		memcpy(buff, header->get_header(), HEADER_SIZE);
		if (payload != NULL)
			memcpy(buff+HEADER_SIZE, payload->get_payload(),
					header->get_payload_len());
		buff[len] = 0;

		// Send all at once
		int status = send(peer.get_send(), buff, len, 0);
		if (status != len) {
			ostringstream logoss;
			logoss << "Failed to send "
					<< type_to_str(header->get_header_type()) <<
					" request to peer at " << ntohs(peer.get_port());
			error(logoss.str());
			return;
		}
		delete[] buff;
	}

	// Send a header and a payload over an already opened connection.
	void sendOverConnection(int connection, DescriptorHeader* header,
			Payload* payload) {

		// Consolidate the header and payload into one buffer
		long len = HEADER_SIZE;
		if (payload != NULL)
			len+= header->get_payload_len();
		char * buff = new char[len+1];
		memcpy(buff, header->get_header(), HEADER_SIZE);
		if (payload != NULL)
			memcpy(buff+HEADER_SIZE, payload->get_payload(),
					header->get_payload_len());
		buff[len] = 0;

		// Send all at once
		int status = send(connection, buff, len, 0);
		if (status != len) {
			ostringstream logoss;
			logoss << "Failed to send "
					<< type_to_str(header->get_header_type());
			error(logoss.str());
			return;
		}
		delete[] buff;
	}

public:
  	Gnutella(int port = DEFAULT_PORT, bool userNode = false,
  			bool clientNode = false, bool superNode = false,
  			const char *shareDirectory = NULL)
  	{
  		if (shareDirectory == NULL) {
  			stringstream ss;
  			ss << DEFAULT_SHARE_DIRECTORY << "/" << port;
  			m_sharedDirectoryName = ss.str();
  		}
  		else {
  			m_sharedDirectoryName = string(shareDirectory);
  		}

		m_messageCount = 0;
		m_userNode = userNode;

		
		// Open the log
		char str[15];
		sprintf(str,"logs/log_%d",port);
		m_log.open(str,ios::out);
		
		if(!m_log.is_open()) {
	  		cerr << "Could not open log file: " << strerror(errno) << endl;
	  		exit(1);
		}
		
	

		// Acquire the listen socket
		int sock = acquireListenSocket(port);

		// Store this node's information
		m_self = Peer(inet_addr("127.0.0.1"), htons(port), -1, sock);

		// Get the list of filenames that this node is willing to share
		readSharedDirectoryFiles();
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
  		int bytesRead = recv(peer.get_recv(), buffer, HEADER_SIZE, MSG_PEEK);

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
			handleQuery(header, peer);
			break;
		case queryHit:
			oss << "Received QUERYHIT from peer at " << ntohs(peer.get_port());
			log(oss.str());
			handleQueryHit(header, peer);
			break;
		case push:
			oss << "Received PUSH from peer at " << ntohs(peer.get_port());
			log(oss.str());
			//handlePush(connection, header,remoteInfo.sin_addr, remoteInfo.sin_port);
			break;
		case resp:
			// Do nothing
			break;
		case httpget:
			// do nothing, an existing peer should not be sending this message.
			break;
		case httpok:
			// do nothing, an existing peer should not be sending this message.
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
  				// Check to make sure we won't block on the connection
  				int bytesAvailable;
  				if (ioctl(connection, FIONREAD, &bytesAvailable) == 0) {
  					if (bytesAvailable == HEADER_SIZE) {
  		  				Peer peer(remoteInfo.sin_addr.s_addr,
  		  						remoteInfo.sin_port);
  		  				peer.set_recv(connection);
  		  				DescriptorHeader *message = readDescriptorHeader(peer);

  		  				if (message != NULL &&
  		  						message->get_header_type() == con) {
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
  		  						// Open sending connection of peer
  								int s = acquireSendSocket();
  								peer.set_send(s);

  								if (connectToPeer(peer)) {
  									// Send an acknowledgement of the CONNECT
  									// request
  									DescriptorHeader response(resp);
  									sendToPeer(peer, &response, NULL);

  									ostringstream new_peer_oss;
  									new_peer_oss << "Adding new peer.";
  									log(new_peer_oss.str());
  									m_peers.insert(peer);
  								}
  		  					}
  		  					// Else the peer is known, update it
  		  					else if (setIter != m_peers.end()) {
  		  						close(setIter->get_recv());
  		  						close(setIter->get_send());
  		  						m_peers.erase(setIter);
  		  						int s = acquireSendSocket();
  		  						peer.set_send(s);

  		  						if (connectToPeer(peer)) {
									// Send an acknowledgement of the CONNECT
									// request
									DescriptorHeader response(resp);
									sendToPeer(peer, &response, NULL);

									ostringstream new_peer_oss;
									new_peer_oss << "Updating peer.";
									log(new_peer_oss.str());
									m_peers.insert(peer);
  		  						}
  		  						else {
  		  							// Close connections
  		  							close(peer.get_recv());
  		  							close(peer.get_send());
  		  						}
  		  					}
  		  					// Max peers have been reached
  		  					else {
  		  						// Reject the connection
  		  						close(connection);
  		  					}
  		  				}
						// Else this is a HTTP GET message
  		  				
  		  				 else if (message != NULL &&
  		  						message->get_header_type() == httpget) {
  		  					ostringstream connect_oss;
  		  					connect_oss << "Received HTTPget from peer at " <<
  									ntohs(message->get_port()) << ". ";
  		  					log(connect_oss.str());

  		  					// Do stuff with the GET message, both send and
  		  					// receives happen on 'connection' variable.
  		  					//Close the connection when done.
  		  					 handleHTTPget(connection, message, peer);
  		  					 
							}
						else if (message != NULL &&
  		  						message->get_header_type() == httpok) {
  		  					ostringstream connect_oss;
  		  					connect_oss << "Received HTTPok from peer at " <<
  									ntohs(message->get_port()) << ". ";
  		  					log(connect_oss.str());

  		  					// Do stuff with the OK message, both send and
  		  					// receives happen on 'connection' variable.
  		  					//Close the connection when done.
  		  					 handleHTTPok(connection, message, peer);
  		  					 
							}


  		  				 

  		  				delete message;
  					}
  				}
  				else {
  					error("ioctl failed while accepting new connection");
  				}
  			}
  			else {
  				// Check if any peers sent data on their connections
  				pollfd *pfds;
  				pfds = new pollfd[m_peers.size()];
  				int i = 0;

  				for (set<Peer>::iterator iter = m_peers.begin();
  						iter != m_peers.end(); ++iter)
  				{
  					pfds[i].fd = iter->get_recv();
  					pfds[i].events = POLLIN;
  					i++;
  				}

  				int rv = poll(pfds, m_peers.size(), timeout * 100);
  				i = 0;

  				if (rv > 0) {
  					for (set<Peer>::iterator iter = m_peers.begin();
  							iter != m_peers.end(); ++iter)
  					{
  						if (pfds[i].fd == iter->get_recv() &&
  								(pfds[i].revents & POLLIN))
  						{
  							int bytesAvailable;
  							if (ioctl(iter->get_recv(), FIONREAD,
  									&bytesAvailable) == 0)
  							{
								// If the remote end closed the connection,
  								// close this end's socket and delete the peer.
								if (bytesAvailable == 0) {
									ostringstream oss;
									oss << "Peer " << ntohs(iter->get_port()) <<
											" closed the connection.";
									log(oss.str());

									close(iter->get_recv());
									close(iter->get_send());
									m_peers.erase(iter);
								}
								else if (bytesAvailable >= HEADER_SIZE) {
									handleRequest(*iter);
								}
  							}
  							else
  							{
  								error("ioctl failed in accepting peer connections");
  								ostringstream oss;
								oss << "Peer " << ntohs(iter->get_port()) <<
										" closed the connection.";
								log(oss.str());

								close(iter->get_recv());
								close(iter->get_send());
								m_peers.erase(iter);
  							}
  						}
  						i++;
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
  		Peer peer(inet_addr(address), htons(port), s, -1);

  		gnutellaConnect(&peer);
  	}

  	/**
  	 * Send a PING message to all known peers.  This function should be
  	 * called in the main listening loop, after connections have been accepted.
  	 */
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

		// Search for the target peer in the sent QUERY map
		map<Peer,map<MessageId,Peer> >::iterator x = m_sentQueryMap.find(peer);

		// If there isn't a map from the message id to a peer already
		// associated with the target peer, create one.
		if (x == m_sentQueryMap.end()) {
			map<MessageId,Peer> idToPeer;
			idToPeer.insert(pair<MessageId,Peer>
					(header.get_message_id(), m_self));
			m_sentQueryMap.insert(pair<Peer,map<MessageId,Peer> >
					(peer,idToPeer));
		}
		// There was a map, but the message id wasn't in the inner map, add it.
		else {
			map<MessageId,Peer>::iterator y =
					x->second.find(header.get_message_id());

			if (y == x->second.end()) {
				// Clear out old QUERYs
				while(x->second.size() >= MAX_PING_STORAGE) {
					x->second.erase(x->second.begin());
				}

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

	// This function handles the user controlled
	void userNode()
	{
		string str;
		while (true) {
			cout << "1) Ping\n2) Query\n3) Review Query Hits\n4) List Shared\
Files\n5) Accept Connections\n6) Exit to network.sh\n#? ";
			cin >> str;
			if (str == "1") {
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
			else if (str == "2") {
				cout << "Please enter a filename to search for\n#? ";
				cin >> str;
				for (set<Peer>::iterator it = m_peers.begin();
										it != m_peers.end(); it++)
					sendQuery(*it, str);

				
				
				acceptConnections(SENDQUERY_TIMEOUT);
			}
			else if (str == "3") {
				if (m_queryHits.size() == 0) {
					cout << "No query hits available.\n";
					continue;
				}
				size_t i = 1;
				for (vector<QueryHit_Payload>::iterator q = m_queryHits.begin();
						q != m_queryHits.end(); q++) {
					in_addr addr;
					addr.s_addr = q->get_ip_addr();
					cout << i << ". " << inet_ntoa(addr) << " "
							<< ntohs(q->get_port()) << "\n";
					size_t j = 1;
					for (vector<Result>::iterator r =
							q->get_result_set().begin();
							r != q->get_result_set().end(); r++) {
						cout << "\t" << j << ". " << r->get_file_name() << " "
								<< r->get_file_size() << "\n";
						j++;
					}
					i++;
				}
				cout << "Select the file you wish to download using this \
format: <peer #>.<file #>\n";
				while (true) {
					cout << "#? ";
					cin >> str;
					size_t delim = str.find_first_of('.');
					if (delim != string::npos) {
						int peer_num = atoi(str.substr(0,delim).c_str());
						int file_num = atoi(str.substr(delim+1,str.length()).c_str());
						if (peer_num > 0 &&
								(size_t) peer_num <= m_queryHits.size()) {
							QueryHit_Payload payload = m_queryHits.at(peer_num-1);
							vector<Result> results = payload.get_result_set();
							if (file_num > 0 &&
									(size_t) file_num <= results.size()) {
								vector<Result> file;
								file.push_back(results.at(file_num-1));
								sendHTTPget(payload.get_port(),
										payload.get_ip_addr(), file);
								acceptConnections(HTTP_TIMEOUT);
								break;
							}
							else {
								cout << "Bad input\n";
							}
						}
						else {
							cout << "Bad input\n";
						}
					}
					else {
						cout << "Bad input\n";
					}
				}
			}
			else if (str == "4") {
				for (vector<SharedFile>::iterator f = m_fileList.begin();
						f != m_fileList.end(); f++) {
					cout << f->getFileName() << " " << f->getBytes() << "\n";
				}
			}
			else if (str == "5") {
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
			else if (str == "6")
				return;
			else
				cout << "Bad option\n";
		}
	}

	void setSharedDirectoryName(const char *directoryName) {
		m_sharedDirectoryName = string(directoryName);
	}
};

int main(int argc, char **argv) {
  // Options
  unsigned short listeningPort = DEFAULT_PORT;
  string bootstrapAddr = "";
  unsigned short bootstrapPort = 0;
  bool userNode = false;
  bool superNode = false;
  bool clientNode = false;
  string sharedDirectory = "";

  const char *optString = "usc";

  const struct option longOpts[] = {
		  {"listen", required_argument, NULL, 0},
		  {"bootstrap", required_argument, NULL, 0},
		  {NULL, no_argument, NULL, 0}
  };

  int longIndex = 0;
  int opt = getopt_long(argc, argv, optString, longOpts, &longIndex);

  while (opt != -1) {
	  switch (opt) {
	  case 'u':
		  userNode = true;
		  break;
	  case 's':
		  if (userNode) {
			  cerr << "A super-node cannot be a user-controlled node." << endl;
			  exit(1);
		  }

		  superNode = true;
		  break;
	  case 'c':
		  if (superNode) {
			  cerr << "A super-node cannot be a client node." << endl;
			  exit(1);
		  }

		  clientNode = true;
		  break;
	  case 0:
		  if (strcmp("listen", longOpts[longIndex].name) == 0) {
			  // Get the listen port
			  listeningPort = atoi(optarg);

			  if (listeningPort <= 0 || listeningPort >= 65535) {
				  cerr << "Invalid listening port." << endl;
				  exit(1);
			  }
		  }
		  else if (strcmp("bootstrap", longOpts[longIndex].name) == 0) {
			  // Get the bootstrap address and port
			  string bootstrapAddrPort(optarg);
			  // Split the address and port
			  int pos = bootstrapAddrPort.find_first_of(':');
			  bootstrapAddr = bootstrapAddrPort.substr(0, pos);
			  bootstrapPort = atoi(bootstrapAddrPort.substr(pos + 1).c_str());

			  if (bootstrapPort <= 0 || bootstrapPort >= 65535) {
				  cerr << "Invalid bootstrap port." << endl;
				  exit(1);
			  }
		  }
		  else if (strcmp("dir", longOpts[longIndex].name) == 0) {
			  sharedDirectory = optarg;
		  }
		  break;
	  default:
		  break;
	  }

	  opt = getopt_long(argc, argv, optString, longOpts, &longIndex);
  }

  // Supernode cannot be a user node
  if (superNode == true && userNode == true) {
	  cerr << "A supernode cannot be user-controlled." << endl;
	  exit(1);
  }

  // Supernode cannot be a client node
  if (superNode == true && clientNode == true) {
	  cerr << "A supernode cannot be a client node." << endl;
	  exit(1);
  }


  Gnutella *node;
  if (sharedDirectory == "") {
	  node = new Gnutella(listeningPort, userNode, clientNode, superNode, NULL);
  }
  else {
	  node = new Gnutella(listeningPort, userNode, clientNode, superNode,
			  sharedDirectory.c_str());
  }

  // Bootstrap if a non-default port
  if (bootstrapAddr != "") {
	  node->bootstrap(bootstrapAddr.c_str(), bootstrapPort);
  }

  // If a user-controlled node, call the user routine
  if (userNode) {
	  node->userNode();
  }
  // If a client node, accept connections without a periodic ping
  else if (clientNode || superNode) {
	  while (true) {
		  node->acceptConnections();
	  }
  }
  else {
	  node->acceptConnections(BOOT_WAIT);
	  while (true) {
		  node->periodicPing();
	  	  node->acceptConnections(PERIODIC_PING);
	  }
  }


  /*
  // User-controlled node with a non-default shared directory
  if (argc >= 6 && strcmp(argv[4], "user") == 0) {
	  node = new Gnutella(atoi(argv[1]), true, argv[5]);
  }
  // User-controlled node with the default shared directory
  else if (argc >= 5 && strcmp(argv[4], "user") == 0) {
	  node = new Gnutella(atoi(argv[1]), true);
  }
  // Dummy node with a non-default port
  else if (argc >= 2) {
	  node = new Gnutella(atoi(argv[1]));
  }
  // Dummy node with a default port
  else {
	  node = new Gnutella();
  }

  // Bootstrap if on a non-default port
  if (argc >= 4) {
	node->bootstrap(argv[2], atoi(argv[3]));
  }

  // If a user-controlled node, call the appropriate routine
  if (argc >= 5 && strcmp(argv[4], "user") == 0) {
	  node->activateUserNode();
	  node->userNode();
  }
  // Otherwise continuously ping and accept connections
  else {
	  while (true) {
		  node->periodicPing();
		  node->acceptConnections(PERIODIC_PING);
	  }
  }*/

  delete node;

  return 0;
}
