#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <time.h>

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "duckchat.h"
#include "server.h"

#define true 1
#define false 0
#define BUFSIZE 1024
#define TIMESIZE 100
#define SRV_PORT "2222"

#define ANSI_COLOR_RED 		"\x1b[31m"
#define ANSI_COLOR_GREEN 	"\x1b[32m"
#define ANSI_COLOR_YELLOW 	"\x1b[33m"
#define ANSI_COLOR_BLUE		"\x1b[34m"
#define ANSI_COLOR_MAGENTA	"\x1b[35m"
#define ANSI_COLOR_CYAN		"\x1b[36m"
#define ANSI_COLOR_RESET	"\x1b[0m"

// :: GLOBAL VALUES :: //
int sockfd= 0;
struct addrinfo *servinfo= NULL;
struct sockaddr_storage lastUser;
size_t lastSize= sizeof(lastUser);

std::map<std::string, std::string> 			map_addrToUser;
std::map<std::string, std::string>			map_userToAddr;
std::multimap<std::string, std::string>		map_userToChan;
std::multimap<std::string, std::string> 	map_chanToUser;

char buf[1024];


// :: CONSTANT VALUES :: //
const char* const REQ_STR[8] = {"REQ_LOGIN",
								"REQ_LOGOUT",
								"REQ_JOIN",
								"REQ_LEAVE",
								"REQ_SAY",
								"REQ_LIST",
								"REQ_WHO",
								"REQ_KEEP_ALIVE"
								};
const char* const TXT_STR[4] = {"TXT_SAY",
								"TXT_LIST",
								"TXT_WHO",
								"TXT_ERROR"
								}; 



// :: FUNCTION PROTOTYPES :: //
std::string addrToString(const struct sockaddr_in*);
std::string addrToUser(const struct sockaddr_in*);
int addUser(const char*);
int addUserToChannel(const char*, const char*);
void log(FILE*, const char*, const char*, const char*);
void logError(const char*);
void logInfo(const char*);
void logReceived(int, const char*);
void logSent(const char*);
void logWarning(const char*);
// TODO add msg_* functs here
char *new_timeStr();
int removeLastUser();
int removeUserFromChannel(const char*, const char*);
int sendMessage(const struct sockaddr*, size_t, struct text*, int);
int sendToLast(struct text*, int);
int setupSocket(char*, char*);
int switchRequest(struct request*, int);


// :: PROGRAM ENTRY :: //
int main(int argc, char **argv) {

	if (argc != 2+1){
		printf("%s", "usage: hostname port\n");
		return 1;
	}

	logInfo("Starting Duckchat Server");

	setupSocket(argv[1], argv[2]);
	logInfo("Sockets initialized");
	logInfo("Waiting for requests");	

	int numbytes= 0;
	while (true) {

	// program logic goes here
		struct request *req= (struct request*) malloc(sizeof (struct request) + BUFSIZE); 
		if ((numbytes= recvfrom(sockfd, req, 1024, 0, (struct sockaddr*)&lastUser, &lastSize)) > 0) {
			switchRequest(req, numbytes);
		}
		free(req);
	}

	freeaddrinfo(servinfo);
	return 0;
}

std::string addrToString(const struct sockaddr_in* addr) {
	char *addrCstr= (char*) malloc(sizeof(char)*BUFSIZE);
	inet_ntop(AF_INET, &(addr->sin_addr), addrCstr, BUFSIZE);
	std::string ipStr= addrCstr;
	free(addrCstr);
	
	char port[6];
	snprintf(port, 6, "%hu", addr->sin_port);
	ipStr= (ipStr + ":" + port);
	return ipStr;
}

std::string addrToUser(const struct sockaddr_in* addr) {
	std::string addrStr= addrToString(addr);
	return map_addrToUser[addrStr];
}

int addUser(const char* username) {
	if (username == NULL) {
		logError("addUser: username was NULL");
		return false;
	}

	struct sockaddr_in* sa= (struct sockaddr_in*)&lastUser;
	char *format= (char*) malloc(BUFSIZE);
	std::string userStr= username;
	
	std::string lastAddr=addrToString((struct sockaddr_in*)&lastUser);
	
	std::string storedAddr= map_userToAddr[userStr];
	
	if (storedAddr != "" && lastAddr != storedAddr) {
		snprintf(format, BUFSIZE, "Duplucate username %s: replacing record %s with %s",
			username, storedAddr.c_str(), lastAddr.c_str());
		logWarning(format);
		map_addrToUser.erase(storedAddr);
	} else {
		snprintf(format, BUFSIZE, "Logging in user %s with record %s",
			username, lastAddr.c_str());
		logInfo(format);
	}

	map_addrToUser[lastAddr]= userStr;
	map_userToAddr[userStr]= lastAddr;
	
	free(format);
	return true;
}

int addUserToChannel(const char *username, const char *channel) {
	if (username == NULL ) {
		logError("addUserToChannel: username was NULL");
		return false;
	} else if (channel == NULL) {
		logError("addUserToChannel: channel was NULL");
		return false;
	}

	char *format= (char*) malloc(sizeof(char) * BUFSIZE);

	std::string userStr= username;
	std::string chanStr= channel;
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;
	bool seen= false;
	
	// Check for channel dups in userToChan; add if none
	ii= map_userToChan.equal_range(userStr);
	for (it= ii.first; it != ii.second; it++) {
		if (it->second == chanStr) { 
			seen= true;
			snprintf(format, BUFSIZE, "User %s already belongs to channel %s; ignoring join request", username, channel);
			logWarning(format);
			break; 
		}
	}
	if (!seen) { 
		map_userToChan.insert( std::pair<std::string,std::string>(userStr, chanStr) ); 
		snprintf(format, BUFSIZE, "Adding user %s to channel %s", username, channel);
		logInfo(format);
	}

	// Check for username dups in chanToUser; add if none
	seen= false;
	ii= map_chanToUser.equal_range(chanStr);
	for (it= ii.first; it != ii.second; it++) {
		if (it->second == userStr) {
			seen= true;
			break;
		}
	}
	if (!seen) { 
		map_chanToUser.insert( std::pair<std::string,std::string>(chanStr, userStr) ); 
	}

	free(format);
	return true;
}

/*
	log - Logs the given output to the specified file stream, with formatting.
		out:		The file stream to log to
		header:		The log message header to print
		msg:		The message to log
		[color]:	The optional ANSI color to use
*/
void log(FILE *out, const char *header, const char *msg, const char *color=ANSI_COLOR_RESET) {
	char *timeStr= new_timeStr();
	fprintf(out, "%s%s [%s] %s%s\n", color, header, timeStr, msg, ANSI_COLOR_RESET);
	free(timeStr);
}

void logError(const char *msg) {
	log(stdout, " [ERR] ::", msg, ANSI_COLOR_RED);
}

/*
	logInfo - Logs the given informational message.
		msg:		The message to log
*/
void logInfo(const char *msg) {
	log(stdout, "[INFO] ::", msg, ANSI_COLOR_GREEN);
}


/*
	logReceived - Logs a message received from the clients.
		type:		The request type
		msg:		The remainder of the request
*/
void logReceived(int type, const char *msg) {
	char *format= (char*) malloc(BUFSIZE * sizeof(char));
	snprintf(format, BUFSIZE, "[%s] \"%s\"", REQ_STR[type], msg);
	log(stdout, "[RECV] ::", format, ANSI_COLOR_CYAN);
	free(format);
}

void logSent(int type, const char *msg) {
	char *format= (char*) malloc(BUFSIZE * sizeof(char));
	snprintf(format, BUFSIZE, "[%s] \"%s\"", TXT_STR[type], msg);
	log(stdout, "[SEND] ::", format, ANSI_COLOR_MAGENTA);
	free(format);	
}

void logWarning(const char* msg) {
	log(stdout, "[WARN] ::", msg, ANSI_COLOR_YELLOW);
}

int msg_error(const char*msg) {
	if (msg == NULL) {
		logError("msg_error: msg was NULL");
		return false;
	}
	struct text_error *txt= (struct text_error*) malloc(sizeof(struct text_error));
	txt->txt_type= htonl(TXT_ERROR);
	strncpy(txt->txt_error, msg, SAY_MAX);

	int result= sendToLast((struct text*) txt, sizeof(struct text_error));
	free(txt);
	logSent(TXT_ERROR, msg);
	return result;
}

int msg_list() {
	char *format= (char*) malloc(BUFSIZE * sizeof(char));

	std::set<std::string> channels;
	std::multimap<std::string,std::string>::iterator it;
	std::set<std::string,std::string>::iterator sit;
	for (it= map_chanToUser.begin(); it != map_chanToUser.end(); it++) {
		channels.insert(it->first);
	}

	if (channels.size() == 0) {	// Should be impossible to reach, really.
		msg_error("No channels exist on this server");
		return false;
	}

	struct text_list *txt= (struct text_list*) malloc(sizeof(struct text_list) + channels.size()*sizeof(struct channel_info));
	txt->txt_type= htonl(TXT_LIST);
	txt->txt_nchannels= htonl(channels.size());

	int i= 0;
	for (sit= channels.begin(); sit != channels.end(); sit++) {
		strncpy(txt->txt_channels[i++].ch_channel, sit->c_str(), CHANNEL_MAX);
	}

	int result= sendToLast((struct text*)txt, sizeof(struct text_list) + channels.size()*sizeof(struct channel_info));
	if (result == true) {
		snprintf(format, BUFSIZE, "%d channels sent", channels.size());
		logSent(TXT_LIST, format);
	}

	free(format);
	free(txt);
	return true;
}

int msg_say(const char *channel, const char *fromUser, const char *msg, const struct sockaddr *addr) {
	struct text_say *txt= (struct text_say*) malloc(sizeof(struct text_say));
	char *format= (char*) malloc(sizeof(char)*BUFSIZE);

	txt->txt_type= htonl(TXT_SAY);
	strncpy(txt->txt_channel, channel, CHANNEL_MAX);
	strncpy(txt->txt_username, fromUser, USERNAME_MAX);
	strncpy(txt->txt_text, msg, SAY_MAX);

	int result= sendMessage(addr, sizeof(struct sockaddr_in), (struct text*)txt, sizeof(struct text_say));
	if (result == true) {
		snprintf(format, BUFSIZE, "[%s][%s] %s", channel, fromUser, msg);
		logSent(TXT_SAY, format);
	}

	free(txt);
	free(format);
	return result;
}

int msg_who(const char *channel) {
	if (channel == NULL) {
		logError("msg_who: channel was NULL");
		return false;
	}

	char *format= (char*) malloc(BUFSIZE * sizeof(char));
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;
	std::string chanStr= channel;

	int numUsers= 0;
	ii= map_chanToUser.equal_range(chanStr);
	for (it= ii.first; it != ii.second; it++) {
		++numUsers;
	}

	if (numUsers == 0) {
		snprintf(format, BUFSIZE, "Channel %s does not exist", channel);
		msg_error(format);
		return false;
	}

	struct text_who *txt= (struct text_who*) malloc(sizeof(struct text_who) + numUsers*sizeof(struct user_info));
	txt->txt_type= htonl(TXT_WHO);
	txt->txt_nusernames= htonl(numUsers);
	strncpy(txt->txt_channel, channel, CHANNEL_MAX);
	int i= 0;
	ii= map_chanToUser.equal_range(chanStr);
	
	for (it=ii.first; it != ii.second; it++) {
		strncpy(txt->txt_users[i++].us_username, it->second.c_str(), USERNAME_MAX);
	}

	int result= sendToLast((struct text*)txt, sizeof(struct text_who) + numUsers*sizeof(struct user_info));
	if (result == true) {
		snprintf(format, BUFSIZE, "<%s> %d users sent", channel, numUsers);
		logSent(TXT_WHO, format);
	}

	free(txt);
	free(format);
	return result;
}

/*
	new_timeStr - Creates a new string giving the time of day.
	Returns: A new heap-allocated char*.

	NOTE: The caller is obligated to free this returned value.
*/
char *new_timeStr() {
	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);
	timeinfo= localtime(&rawtime);

	char *timeStr= (char*) malloc(TIMESIZE * sizeof(char));
	strftime(timeStr, TIMESIZE, "%X %x", timeinfo);
	return timeStr;
}

int recv_join(struct request_join *req) {
	logReceived(REQ_JOIN, req->req_channel);
	std::string userStr= addrToUser((struct sockaddr_in*)&lastUser);
	return addUserToChannel(userStr.c_str(), req->req_channel);
}

int recv_keepAlive(struct request_keep_alive *req) {
	logReceived(REQ_KEEP_ALIVE, "");
	return true;
}

int recv_leave(struct request_leave *req) {
	logReceived(REQ_LEAVE, req->req_channel);
	std::string userStr= addrToUser((struct sockaddr_in*)&lastUser);
	return removeUserFromChannel(userStr.c_str(), req->req_channel);
}

int recv_list(struct request_list *req) {
	logReceived(REQ_LIST, "");
	return msg_list();
}

int recv_login(struct request_login *req) {
	logReceived(REQ_LOGIN, req->req_username);
	addUser(req->req_username);
	return true;
}

int recv_logout(struct request_logout *req) {
	logReceived(REQ_LOGOUT, "");

	int result= false;
	std::string userStr= addrToUser((struct sockaddr_in*)&lastUser);
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;
	ii= map_userToChan.equal_range(userStr);


	for (it= ii.first; it != ii.second; it++) {
		result= removeUserFromChannel(userStr.c_str(), it->second.c_str()) 
				&& result;
	}

	result= removeLastUser() && result;
	return result;
}

int recv_say(struct request_say *req) {
	char *msg= (char*) malloc(BUFSIZE * sizeof(char));
	std::string fromUser= addrToUser((struct sockaddr_in*)&lastUser);
	
	sprintf(msg, "<%s> %s", req->req_channel, req->req_text);
	logReceived(REQ_SAY, msg);
	free(msg);
	
	// for every channel the user is a member of...
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> cii;
	std::multimap<std::string,std::string>::iterator cit;

	std::string req_channel= req->req_channel;
	cii= map_chanToUser.equal_range(req_channel);
	
	for (cit= cii.first; cit != cii.second; cit++) {
		std::string userStr= cit->second;

		std::string userAddr= map_userToAddr[userStr];
		char *tok= (char*) malloc(sizeof(char)*BUFSIZE);
		strncpy(tok, userAddr.c_str(), strlen(userAddr.c_str()));
		
		char *ip= strtok(tok, ":");
		char *port= strtok(NULL, ":");
		u_short p= (u_short) atoi(port);

		struct sockaddr_in sa;
		inet_pton(AF_INET, ip, &(sa.sin_addr));
		sa.sin_port= p;
		sa.sin_family= AF_INET;

		msg_say(req->req_channel, fromUser.c_str(), req->req_text, (struct sockaddr*)&sa);
	}
	
	
		// for every user in that channel...
	
			// sendto their stored address (hopefully)...
	return false; //msg_say(req->;
}

int recv_who(struct request_who *req) {
	logReceived(REQ_WHO, req->req_channel);
	return msg_who(req->req_channel);
}
int removeLastUser() {
	std::string addrStr= addrToString((struct sockaddr_in*)&lastUser);
	std::string userStr= map_addrToUser[addrStr];
	if (userStr == "") { // Unusual state; no user to remove
		logError("removeLastUser: Unknown source record to remove");
		return false;
	}

	map_addrToUser.erase(addrStr);
	map_userToAddr.erase(userStr);
	char *format= (char*) malloc(BUFSIZE * sizeof(char));
	snprintf(format, BUFSIZE, "Logging out user %s", userStr.c_str());
	logInfo(format);
	free(format);

	return true;	
}

int removeUserFromChannel(const char *username, const char *channel) {
	if (username == NULL) {
		logError("removeUserFromChannel: username was NULL");
		return false;
	} else if (channel == NULL || strlen(channel) > CHANNEL_MAX) {
		logError("removeUserFromChannel: channel was NULL");
		return false;
	}

	std::string userStr= username;
	std::string chanStr= channel;
	char *format= (char*) malloc(sizeof(char) * BUFSIZE);

	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;
	bool seen= false;	

	// Remove first matching chanel from userToChan
	ii= map_userToChan.equal_range(userStr);
	for (it= ii.first; it != ii.second; it++) {
		if (it->second == chanStr) {
			seen= true;
			map_userToChan.erase(it);
			snprintf(format, BUFSIZE, "Removed channel %s from user %s", it->second.c_str(), it->first.c_str());
			logInfo(format);
			break;
		}
	}
	if (!seen) {
		snprintf(format, BUFSIZE, "While removing, did not find channel %s recorded for user %s", channel, username);
		logWarning(format);
	}

	// Remove first matching user from chanToUser
	ii= map_chanToUser.equal_range(chanStr);
	seen= false;
	for (it= ii.first; it != ii.second; it++) {
		if (it->second == userStr) {
			seen= true;
			map_chanToUser.erase(it);
			snprintf(format, BUFSIZE, "Removed user %s from channel %s", it->second.c_str(), it->first.c_str());
			logInfo(format);
			break;
		}
	}
	if (!seen) {
		snprintf(format, BUFSIZE, "While removing, did not find user %s recorded for channel %s", username, channel);
		logWarning(format);
	}

	free(format);
	return true;
}

int sendMessage(const struct sockaddr *addr, size_t addrlen, struct text *msg, int msglen) {
	int result= sendto(sockfd, msg, msglen, 0, addr, addrlen);
	if (result == -1) {
		perror("sendto: ");
		return false;
	}
	return true;
}

int sendToLast(struct text *msg, int msglen) {
	return sendMessage((struct sockaddr*)&lastUser, lastSize, msg, msglen);
}

/*
	setupSocket - Creates the central socket for use by the server.
	Returns: true or false, regarding socket creation success.
*/
int setupSocket(char *addr, char *port) {
	int result= 0;

	// Setup hints
	struct addrinfo hints;
	memset(&hints, 0, sizeof hints);
	hints.ai_family= AF_INET;
	hints.ai_socktype= SOCK_DGRAM;
	hints.ai_flags= AI_PASSIVE;

	// Fetch address info struct
	if ((result= getaddrinfo(addr, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
		return false;
	}

	// Create UDP socket
	sockfd= socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
	if (sockfd == -1) {
		perror("socket: ");
		return false;
	}

	if ((bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen)) == -1) {
		perror("bind: ");
		return false;
	}

	return true;
}

int switchRequest(struct request* req, int len) {
	char *warning= (char*) malloc(sizeof(char)*BUFSIZE);
	int result= false;

	if (addrToUser((struct sockaddr_in*)&lastUser) == "" && req->req_type != REQ_LOGIN) {
		snprintf(warning, BUFSIZE, "Received request type %d from unknown user; ignoring", ntohl(req->req_type));
		logWarning(warning);
		free(warning);
		return result;
	}

	switch( ntohl(req->req_type) ) {
	case REQ_LOGIN:
		if (sizeof(struct request_login) != len) {
			snprintf(warning, BUFSIZE, "Received login request; expected %d bytes but was %d bytes", sizeof(struct request_login), len);
			logWarning(warning);
			result= false;
			break;
		}
		result= recv_login( (struct request_login*) req );
		break;
	case REQ_LOGOUT:
		if (sizeof(struct request_logout) != len) {
			snprintf(warning, BUFSIZE, "Received logout request; expected %d bytes but was %d bytes", sizeof(struct request_logout), len);
			logWarning(warning);
			result= false;
			break;
		}
		result= recv_logout( (struct request_logout*) req );
		break;
	case REQ_JOIN:
		if (sizeof(struct request_join) != len) {
			snprintf(warning, BUFSIZE, "Received join request; expected %d bytes but was %d bytes", sizeof(struct request_join), len);
			logWarning(warning);
			result= false;
			break;
		}
		result= recv_join( (struct request_join*) req );
		break;
	case REQ_LEAVE:
		if (sizeof(struct request_leave) != len) {
			snprintf(warning, BUFSIZE, "Received leave request; expected %d bytes but was %d bytes", sizeof(struct request_leave), len);
			logWarning(warning);
			result= false;
			break;
		}
		result= recv_leave( (struct request_leave*) req );
		break;
	case REQ_SAY:
		if (sizeof(struct request_say) != len) {
			snprintf(warning, BUFSIZE, "Received say request; expected %d bytes but was %d bytes", sizeof(struct request_say), len);
			logWarning(warning);
			result= false;
			break;
		}
		result= recv_say( (struct request_say*) req );
		break;
	case REQ_LIST:
		if (sizeof(struct request_list) != len) {
			snprintf(warning, BUFSIZE, "Received list request; expected %d bytes but was %d bytes", sizeof(struct request_list), len);
			logWarning(warning);
			result= false;
			break;
		}
		result= recv_list( (struct request_list*) req );
		break;
	case REQ_WHO:
		if (sizeof(struct request_who) != len) {
			snprintf(warning, BUFSIZE, "Received who request; expected %d bytes but was %d bytes", sizeof(struct request_who), len);
			logWarning(warning);
			result= false;
			break;
		}
		result= recv_who( (struct request_who*) req );
		break;
	case REQ_KEEP_ALIVE:
		if (sizeof(struct request_keep_alive) != len) {
			snprintf(warning, BUFSIZE, "Received keepalive request; expected %d bytes but was %d bytes", sizeof(struct request_keep_alive), len);
			logWarning(warning);
			result= false;
			break;
		}
		result= recv_keepAlive( (struct request_keep_alive*) req );
		break;
	default:
		snprintf(warning, BUFSIZE, "Received unknown request type %d of size %d bytes", req->req_type, len);
		logWarning(warning);
		break;
	}
	free(warning);
	return result;
}
