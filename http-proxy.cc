/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <string>
#include <map>
#include <chrono>
#include <ctime>
#include "http-request.h"
#include "http-response.h"
using namespace std;
using namespace std::chrono;

const string GET_ERROR						= "Request is not GET";
const string NOT_IMPLEMENTED_MSG	= "Not Implemented";
const string BAD_REQUEST_MSG			= "Bad Request";
const string NOT_IMPLEMENTED_CODE = "501";
const string BAD_REQUEST_CODE		 = "400";
const char* NON_PERSISTENT				= "1.0";
const char* PERSISTENT						= "1.1";
const int TIMEOUT_TIME            = 3 * 1000; // milliseconds
const int POLL_TIMEOUT            = 1 * 60 * 1000; // milliseconds

const char* PORT_PROXY_LISTEN		 = "14886";
const short PORT_SERVER_DEFAULT	 = 80;
const int BUFSIZE								 = 1024;
const int BACKLOG								 = 20;
const string DONT_CACHE 					= "-1";

#define CHECK(F) if ( (F) == -1 ) cerr << "Error when calling " << #F << endl;
#define CHECK_CONTINUE(F) if ( (F) == -1 ) { cerr << "Error when calling " << #F << endl; continue; }
#define ERROR(format, ...) fprintf(stderr, format, ## __VA_ARGS__);
#define NOFLAGS 0

typedef struct {
	string response;
	time_point<system_clock> timestamp;
	time_point<system_clock> expires;
} CacheEntry;
typedef map<string, CacheEntry> cache;
typedef time_point<system_clock> timept;

cache g_cache;

void clean_up_socket(int fd) {
  // close(fd);
  while (close(fd) < 0);
	shutdown(fd, 2);
}

int createRemoteSocket(string host, short port) {
	struct addrinfo hints, *server_addr;
	memset(&hints, 0, sizeof hints); // clear struct
	hints.ai_family	 = AF_INET;		 // handle IPv4
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	int status;
	if ( (status = getaddrinfo(host.c_str(), to_string(port).c_str(), &hints, &server_addr)) != 0 )
		ERROR("Invalid remote server %s:%d", host.c_str(), port);

	//Get a file descriptor that we can use to write to the server
	int server_fd;
	CHECK(server_fd = socket(server_addr->ai_family, server_addr->ai_socktype, server_addr->ai_protocol))
	//Now establish a connection with the server at the port the client asked for.
	CHECK(connect(server_fd, server_addr->ai_addr, server_addr->ai_addrlen))
	//We've connected the socket to the remote address. Now it's ready to talk to.
	return server_fd;
}

string readFromSocket(int fd, int waitForBody) {
	//A generic function that simply reads from any socket file into a C++ string
	// Transfer data one KB at a time from C string buffer to C++ string result.
  struct pollfd ufds;
  ufds.fd = fd;
  ufds.events = POLLIN;
  
	char buffer[BUFSIZE + 1];
  bool foundCarriage = false;
	string result;
	int len;
  int rv;
	do {
		memset(&buffer, 0, sizeof buffer);
    if (((rv = poll(&ufds, 1, POLL_TIMEOUT)) > 0) && ((len = read(fd, buffer, sizeof buffer)) > 0))
	    result.append(buffer);
    cerr << "rv: " << rv << endl;
    cerr << "len: " << len << endl;
    cerr << "waitForBody: " << waitForBody << endl;
    foundCarriage = (memmem(result.c_str(), result.length(), "\r\n\r\n", 4) != NULL);
    cerr << "found carriage: " << foundCarriage << endl;
  } while ((rv != 0) && ((!foundCarriage && !waitForBody) || ((len > 0) && waitForBody)));
  cerr << "Outside of loop" << endl;
  cerr << "rv: " << rv << endl;
  cerr << "len: " << len << endl;
  cerr << "waitForBody: " << waitForBody << endl;
  cerr << "found carriage: " << foundCarriage << endl;
	return result;
}


void readResponseAndBody(int server_fd, HttpResponse& response, string& body) {
	//Create an empty HttpResponse object. Call our generic low-level function to read from 
	//the server socket into a C++ string, then push that into the HttpResponse as a C string
	//and return the HttpResponse object.
	//Once again, literally the same thing as readClientRequest, except we have to call different
	//functions here.
	string response_string = readFromSocket(server_fd, true);
  cerr << endl << "RESPONSE STRING IN READ RESPONSE AND BODY" << endl << response_string << endl;
	unsigned body_pos = response_string.find("\r\n\r\n");
  if (body_pos == -1) {
    response_string += "\r\n\r\n";
    body = "";
  }
  else body = response_string.substr(body_pos + 4);
	//Now all of server response is in response_string; load it into an HttpResponse object
	response.ParseResponse(response_string.c_str(), response_string.length());
}

HttpRequest readClientRequest(int client_fd) {
	//Returns HttpRequest object if succesful, throws ParseException up to next level if fails
	string request_string = readFromSocket(client_fd, false);
	cerr << "Finished Reading Client Request:" << endl << request_string << endl;

	//Now create a HttpRequest object to parse the client's request
	//This one is a little more complicated than the other function because we have to check
	//for errors.
	HttpRequest request;
	try {
		request.ParseRequest(request_string.c_str(), request_string.length());
	} catch (ParseException e) {
		throw; //Make caller deal with this, because it'll need to tell the client the request was bad.
	}
	return request;
}

int sendResponseAndBodyToClient(int client_fd, HttpResponse& response, const string& body) {
	//This function simply flattens an HttpResponse object into a C string and sends it.
	char response_buffer[response.GetTotalLength()];
	response.FormatResponse(response_buffer);
  response_buffer[response.GetTotalLength()] = 0;
  cerr << endl << "RESPONSE BUFFER" << endl << response_buffer << endl << "BODY"<< endl << body << endl;
	return write(client_fd, response_buffer, sizeof response_buffer) &&
	write(client_fd, body.c_str(), body.length());
}

int sendRequestToServer(int server_fd, HttpRequest& request) {
	//This function simply flattens an HttpRequest object into a C string and sends it.
	//Note that this function is literally the exact same thing as sendResponseToClient,
	//except we have to use the different functions FormatRequest and FormatResponse,
	//which are not virtual.. this is really icky OO for class HttpHeaders :(
	char request_buffer[request.GetTotalLength()];
	request.FormatRequest(request_buffer);
  request_buffer[request.GetTotalLength()] = 0;
	cerr << "Formatted Proxy Request: " << endl << request_buffer << endl;
  bool b = memmem(request_buffer, request.GetTotalLength(), "\r\n\r\n", 4) != NULL;
  if (b) cerr << endl << "CONTAINS CARRIAGE" << endl;
  else cerr << endl << "CONTAINS CARRIAGE" << endl;
	int w = write(server_fd, request_buffer, sizeof request_buffer);
  cerr << endl << "RESULT OF WRITING TO SERVER: " << w << endl;
  cerr << endl << "WROTE REQUEST TO SERVER" << endl;
  return w;
}

bool needsUpdate(const string& cache_key) {
	//Checks to see if cache_key has an associated cache entry. If yes, check to see if it's expired.
	//Check expired time vs current time to see if we need to check for a new version of the page
	timept now = system_clock::now();
	//If a cache entry exists
	cache::iterator iter = g_cache.find(cache_key);
	bool in_cache =  iter != g_cache.end();
	return !in_cache || now > iter->second.expires; //True if not found or expires before now
}

string extractHost(HttpRequest& request) {
	return (request.GetHost().length() != 0) ? request.GetHost() : request.FindHeader("Host");
}

short extractPort(HttpRequest& request) {
	return (request.GetPort() > 0) ? request.GetPort() : PORT_SERVER_DEFAULT;
}

timept timept_from_string(const string& time_string) {
	//Converts a string like "Mon, 17 Feb 2014 17:32:17 GMT" to time_point<system_clock>
	static const char format[] = "%a, %d %b %Y %H:%M:%S %Z"; // rfc 1123
	tm t;
	strptime(time_string.c_str(), format, &t);
	time_t tt = mktime(&t);
	return system_clock::from_time_t(tt); //Finally return a time_point object
}

timept extractExpireTime(HttpResponse& response) {
	string exptime_string = response.FindHeader("Expires");
  cerr << "Expires: " << exptime_string << endl;
	if ( exptime_string == "" || exptime_string == DONT_CACHE ) //If the page comes with no expiration 
  {
    cerr << "Hello" << endl;
    return  system_clock::now();
  }
	else { //If there is an expiration time in the response
    cerr << "Oh God" << endl;
		return timept_from_string(exptime_string);
	}	
}

string getCacheKey(HttpRequest& request) {
	//Figure out which page the client wants by extracting host, port, and path from the request
	string host = extractHost(request);
	short port = extractPort(request);
	string path = "/"; //temporary
	return host + ":" + to_string(port) + path;
}

int updateCache(HttpRequest& request) {
	//Takes a cilent request and extracts the page page from it to update the cache
	//First use the various parts of request to figure out which page the client wants
	string cache_key = getCacheKey(request);
  cerr << endl << "GOT CACHE KEY: " << cache_key << endl;

	if ( needsUpdate(cache_key) ) { //If cached copy is expired or has never been downloaded
			cerr << "Updating cache for " << cache_key << endl;
			// connect to remote server and return file descriptor	
			string remote_server_host = extractHost(request);
			short remote_server_port = extractPort(request);
			int server_fd = createRemoteSocket(remote_server_host, remote_server_port);
			// foward client request to remote server
      cerr << endl << "CONNECTED TO SERVER ON FD " << server_fd << endl;
			CHECK(sendRequestToServer(server_fd, request))
        cerr << endl << "RETURNED FROM WRITING TO SERVER AND BACK IN UPDATE CACHE" << endl;
			//And get back the response to update cache with
			HttpResponse server_response;
			string body;
      cerr << endl << "BEFORE READ RESPONSE AND BODY" << endl;
			readResponseAndBody(server_fd, server_response, body);
      cerr << "RESPONSE STATUS CODE" << server_response.GetStatusCode() << endl;
      cerr << "BODY IN UPDATECACHE" << endl <<  body << endl;
      cerr << endl << "AFTER READ RESPONSE AND BODY" << endl;
			// close and shutdown connection with remote server
			clean_up_socket(server_fd);
			//Cache entry consists of {response, timestamp, expiration time}
      cerr << "Point A" << endl;
			timept now = system_clock::now(); 
      cerr << "Point B" << endl;
			timept exptime = extractExpireTime(server_response);
      cerr << "Point C" << endl;
			CacheEntry new_entry = {body, now, exptime};
      cerr << "Point D" << endl;
			g_cache[cache_key] = new_entry;
      cerr << "Point E" << endl;
	}
	return 0;
}

int processClient(int client_fd) {
	HttpResponse proxy_response; //Will be used to store both success and failure response
	string body;
  bool persistent = false;
  timept start_time, cur_time;
  int elapsed_time;
  
  do {
  	//First try to read the client's request. If there's a problem, form an error response.
  	try {
  		HttpRequest client_request = readClientRequest(client_fd);
      start_time = system_clock::now(); // mark start_time
      cerr << endl << "GOT CLIENT REQUEST" << endl;
      if (client_request.GetVersion() == NON_PERSISTENT) {
        client_request.ModifyHeader("Connection", "close");
        persistent = false;
      }
      else persistent = true;
      
  		//Update the cached copy if necessary
  		updateCache(client_request);
      cerr << endl << "UPDATED CACHE" << endl;
  		string cache_key = getCacheKey(client_request);
  		//Now it doesn't matter if the cache was updated or not; we give the client the cached copy
  		//of the response/page.
  		body = g_cache[cache_key].response;
      cerr << "GET BODY FROM CACHE" << endl << body << endl;
  		proxy_response.SetStatusMsg("OK");
  		proxy_response.SetStatusCode("200");
  		proxy_response.SetVersion(client_request.GetVersion());
      
  	} //end try
  	catch (ParseException e) {
  		//TODO what if there's an error parsing the server response? :o
  		//Use proxy_response as an error response that will be sent to the client instead of the requested page
  		cerr << "Error Parsing Client Request: " << e.what() << endl;
  		proxy_response.SetVersion(NON_PERSISTENT);
  		bool not_impl = !strcmp(e.what(), GET_ERROR.c_str());
  		proxy_response.SetStatusMsg(not_impl ? NOT_IMPLEMENTED_MSG : BAD_REQUEST_MSG);
  		proxy_response.SetStatusCode(not_impl ? NOT_IMPLEMENTED_CODE : BAD_REQUEST_CODE);
  		body = "";
  		cerr << "Returned Error Response to Client" << endl;
  	}

  	//These things have to be done whether or not the parsing succeeded:
  		// return server response to client
    cerr << endl << "BEFORE SEND RESPONSE TO CLIENT" << endl;
  	CHECK(sendResponseAndBodyToClient(client_fd, proxy_response, body))
    cerr << endl << "SENT RESPONSE TO CLIENT" << endl;
    
  		// close and shutdown connection with client
      cur_time = system_clock::now();
      elapsed_time = duration_cast<milliseconds> (cur_time - start_time).count();
  } while (persistent && (elapsed_time <= TIMEOUT_TIME));
	clean_up_socket(client_fd);
	return -1; //TODO why do we return -1?
}

int createListenSocket() {
/* 
 * This function returns a file descriptor that points to a new socket bound to 
 * localhost:PORT_PROXY_LISTEN. It will be ready to listen for and accept TCP connections. 
 */
	// generate addresses which can bind to a socket for client requests
	struct addrinfo hints, *res, *p;
	memset(&hints, 0, sizeof hints); // clear struct
	hints.ai_family	 = AF_INET;		 // handle IPv4
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	hints.ai_flags		= AI_PASSIVE;	// assign my localhost addr to sockets

	int status;
	if ((status = getaddrinfo(NULL, PORT_PROXY_LISTEN, &hints, &res)) != 0)
		ERROR("getaddrinfo error: %s\n", gai_strerror(status))

	// res now points to a linked list of struct addrinfos, probably just 1 in this case
	// create a socket and bind to a valid address
	int sockfd;
	int yes = 1;
	for (p = res; p != NULL; p = p->ai_next) {
		CHECK(sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol));	// create socket
		CHECK(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));			// allow port reuse
		CHECK(bind(sockfd, res->ai_addr, res->ai_addrlen));													// bind
	}

	freeaddrinfo(res);
	return sockfd;
}

int main (int argc, char *argv[]) {
	// create listen socket
	int sockfd = createListenSocket();

	// first start listening for connections
	CHECK(listen(sockfd, BACKLOG))

	// now loop forever, accepting a connection and forking a new process to deal with it
	//Keep track of the number of forked child processes; we'll be forking once per iteration
	int number_children = 0, status;
	for ( ;; ++number_children ) {
	//Now here's the thing: Before accepting a connection and forking, make sure we haven't
	//exceeded the backlog. If so, loop and wait for zombie children that we can clean up.
	//As soon as we find one, decrease the counter so we can exit the loop.
	//Idea from http://stackoverflow.com/questions/12591540/waitpid-and-fork-to-limit-number-of-child-processes
	for ( ; number_children >= BACKLOG; --number_children )
		wait(&status); //Currently accepts connection and then hangs.
			//TODO Make sure this behavior is OK

		// setup client addr info
		struct sockaddr client_addr;
		memset(&client_addr, 0, sizeof client_addr);
		socklen_t sizevar = (socklen_t) sizeof client_addr;
		int client_fd;

		// accept client connection
		CHECK_CONTINUE(client_fd = accept(sockfd, &client_addr, &sizevar))
		cerr << "Accepted New Connection" << endl;

		// what if fork fails?
		if ( fork() ) {
			cerr << "I'm the parent" << endl;
		}
		else {
			cerr << "I'm a child" << endl;
			processClient(client_fd);
			close(client_fd);
			exit(0);
		}

		// if max # of processes have been forked
		// then we should wait for any process to 
		// finish before continuing
	}
	return 0;
}