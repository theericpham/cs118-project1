/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <iostream>
#include <iomanip>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <string>
#include <chrono>
#include <map>
#include <ctime>
#include "http-request.h"
#include "http-response.h"
using namespace std;
using namespace std::chrono;

const string GET_ERROR            = "Request is not GET";
const string NOT_IMPLEMENTED_MSG  = "Not Implemented";
const string BAD_REQUEST_MSG      = "Bad Request";
const string NOT_IMPLEMENTED_CODE = "501";
const string BAD_REQUEST_CODE     = "400";
const char* NON_PERSISTENT        = "1.0";
const char* PERSISTENT            = "1.1";
const int TIMEOUT_TIME            = 3 * 1000; // milliseconds
const int POLL_TIMEOUT            = 1 * 60 * 1000; // milliseconds
const int MAX_ATTEMPTS            = 3;
const string DONT_CACHE           = "-1";
const char TIME_FORMAT[] = "%a, %d %b %Y %H:%M:%S %Z"; // rfc 1123

const char* PORT_PROXY_LISTEN     = "14886";
const short PORT_SERVER_DEFAULT   = 80;
const int BUFSIZE                 = 1024;
const int BACKLOG                 = 20;

#define CHECK(F) if ( (F) == -1 ) cerr << endl << "Error when calling " << #F << endl;
#define CHECK_CONTINUE(F) if ( (F) == -1 ) { cerr << endl << "Error when calling " << #F << endl; continue; }
#define ERROR(format, ...) fprintf(stderr, format, ## __VA_ARGS__);
#define NOFLAGS 0

typedef struct {
  string response;
  time_point<system_clock> timestamp;
  time_point<system_clock> expires;
} CacheEntry;
typedef map<string, CacheEntry> cache;
typedef time_point<system_clock> timept;

//cache g_cache;
cache g_cache;

void clean_up_socket(int fd) {
  // close(fd);
  while (close(fd) < 0);
  shutdown(fd, 2);
}

string extractHost(HttpRequest& request) {
  return (request.GetHost().length() != 0) ? request.GetHost() : request.FindHeader("Host");
}

short extractPort(HttpRequest& request) {
  return (request.GetPort() > 0) ? request.GetPort() : PORT_SERVER_DEFAULT;
}

// Figure out which page the client wants by extracting host, port, and path from the request
string getCacheKey(HttpRequest& request) {
  string host = extractHost(request);
  short port = extractPort(request);
  string path = request.GetPath();
  return host + ":" + to_string(port) + path;
}

// Converts a string like "Mon, 17 Feb 2014 17:32:17 GMT" to time_point<system_clock>
timept timept_from_string(const string& time_string) { 
  static const char format[] = "%a, %d %b %Y %H:%M:%S %Z"; // rfc 1123
  tm t;
  strptime(time_string.c_str(), format, &t);
  time_t tt = mktime(&t);
  return system_clock::from_time_t(tt); //Finally return a time_point object
}

string string_from_timept(timept& time_tpt) {
	ostringstream os;
	time_t time_tt = system_clock::to_time_t(time_tpt);
	os << put_time(localtime(&time_tt), TIME_FORMAT);
	return os.str();
}

// return the expire time from response header
timept extractExpireTime(HttpResponse& response) {
  string exptime_string = response.FindHeader("Expires");
  if ( exptime_string == "" || exptime_string == DONT_CACHE ) //If the page comes with no expiration 
  {
    return  system_clock::now();
  }
  else { //If there is an expiration time in the response
    return timept_from_string(exptime_string);
  }  
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

int createRemoteSocket(string host, short port) {
	struct addrinfo hints, *server_addr;
	memset(&hints, 0, sizeof hints); // clear struct
	hints.ai_family   = AF_INET;     // handle IPv4
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

/*
 *  Return client request as a string
 */
string readClientRequest(int client_fd) {
  struct pollfd ufds;
  ufds.fd = client_fd;
  ufds.events = POLLIN;
  
  string request;
  char client_request_buffer[BUFSIZE + 1];
  int len;
  int rv;
  
  // append buffer contents to the string until we timeout or find end of request
  do {
    memset(&client_request_buffer, 0, sizeof client_request_buffer);
    if ((rv = poll(&ufds, 1, POLL_TIMEOUT) > 0) && 
      (len = read(client_fd, client_request_buffer, sizeof client_request_buffer) > 0))
      request.append(client_request_buffer);
  } while ((rv != 0) && (memmem(request.c_str(), request.length(), "\r\n\r\n", 4) == NULL));
  
  return request;
}

/*
 *  Return server response as a string
 */
string readServerResponse(int server_fd) {
  struct pollfd ufds;
  ufds.fd = server_fd;
  ufds.events = POLLIN;
  
  string response;
  char server_response_buffer[BUFSIZE + 1];
  int len = 0;
  
  do {
    memset(&server_response_buffer, 0, sizeof server_response_buffer);
    // len = 0;
    sleep(1);
    len = read(server_fd, server_response_buffer, sizeof server_response_buffer);
    response.append(server_response_buffer);
  } while (len >= sizeof server_response_buffer);
  
  return response;
}

int processClient(int client_fd) {
  int elapsed_time = 0;
  time_point<system_clock> start_time, current_time;
  start_time = system_clock::now();
  
  bool persistent = false;
  do {

    // receive client request
    string request, response;
    request = readClientRequest(client_fd);
    int len = request.length();
        
    // process if client made a request
    if (len > 0) {
      start_time = system_clock::now();
      
      // parse client request
      HttpRequest client_request;
      try {
        client_request.ParseRequest(request.c_str(), request.length());
        if (client_request.GetVersion() == NON_PERSISTENT) {
          client_request.ModifyHeader("Connection", "close");
          persistent = false;
        }
        else persistent = true;
      } catch (ParseException e) {
        HttpResponse error_response;
        error_response.SetVersion(NON_PERSISTENT);

        if (strcmp(e.what(), GET_ERROR.c_str()) == 0) {
          error_response.SetStatusMsg(NOT_IMPLEMENTED_MSG);
          error_response.SetStatusCode(NOT_IMPLEMENTED_CODE);
        }
        else {
          error_response.SetStatusMsg(BAD_REQUEST_MSG);
          error_response.SetStatusCode(BAD_REQUEST_CODE);
        }
    
        len = error_response.GetTotalLength();
        char response_buffer[len];
        error_response.FormatResponse(response_buffer);
        write(client_fd, response_buffer, len);
        close(client_fd);
        shutdown(client_fd, 2);  // disallow further sends and receives
      }
       
      string cache_key = getCacheKey(client_request);
      // replace cached response if it's expired
      if (needsUpdate(cache_key)) {
        // format proxy request to send to remote server
        int size = client_request.GetTotalLength();
        char proxy_request_buffer[size];
        client_request.FormatRequest(proxy_request_buffer);
        proxy_request_buffer[size] = 0;  
        // find host and port to connect to
        string remote_server_host;
        short  remote_server_port;
        remote_server_host = (client_request.GetHost().length() != 0) ? client_request.GetHost() : client_request.FindHeader("Host");
        remote_server_port = (client_request.GetPort() > 0) ? client_request.GetPort() : PORT_SERVER_DEFAULT;
  
        // connect to remote server and return file descriptor
        int server_fd = createRemoteSocket(remote_server_host, remote_server_port);
        fcntl(server_fd, F_SETFL, O_NONBLOCK);
  
        // foward client request to remote server
        len = write(server_fd, proxy_request_buffer, client_request.GetTotalLength());
  
        // receive and parse server response
        HttpResponse server_response;
        response = readServerResponse(server_fd);
        try {
          server_response.ParseResponse(response.c_str(), response.length());
        } catch (ParseException e) {
          cerr << endl << "Error Parsing Server Response:" << endl << e.what() << endl;
        }
        
        // mark time of cache and expire
        timept now = system_clock::now(); 
        timept exptime = extractExpireTime(server_response);
        CacheEntry new_entry = {response, now, exptime};
        g_cache[cache_key] = new_entry;  // cache fresh server response
          
        clean_up_socket(server_fd);
      }
      
      string cached_response = g_cache[cache_key].response;
  
      // return server response to client
      while (write(client_fd, cached_response.c_str(), cached_response.length()) < 0);
      current_time = system_clock::now();
      elapsed_time = duration_cast<milliseconds> (current_time - start_time).count();
    }
  } while ((elapsed_time <= TIMEOUT_TIME) && persistent);
  clean_up_socket(client_fd);
  return -1;
}

/* 
 * This function returns a file descriptor that points to a new socket bound to 
 * localhost:PORT_PROXY_LISTEN. It will be ready to listen for and accept TCP connections. 
 */
int createListenSocket() {
  // generate addresses which can bind to a socket for client requests
  struct addrinfo hints, *res, *p;
  memset(&hints, 0, sizeof hints); // clear struct
  hints.ai_family   = AF_INET;     // handle IPv4
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  hints.ai_flags    = AI_PASSIVE;  // assign my localhost addr to sockets
  
  int status;
  if ((status = getaddrinfo(NULL, PORT_PROXY_LISTEN, &hints, &res)) != 0)
    ERROR("getaddrinfo error: %s\n", gai_strerror(status))

  // res now points to a linked list of struct addrinfos, probably just 1 in this case
  // create a socket and bind to a valid address
  int sockfd;
  int yes = 1;
  for (p = res; p != NULL; p = p->ai_next) {
    CHECK(sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol));  // create socket
    CHECK(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));      // allow port reuse
    CHECK(bind(sockfd, res->ai_addr, res->ai_addrlen));                          // bind
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
    // setup client addr info
    struct sockaddr client_addr;
    memset(&client_addr, 0, sizeof client_addr);
    socklen_t sizevar = (socklen_t) sizeof client_addr;
    int client_fd;
    
    // accept client connection
  	CHECK_CONTINUE(client_fd = accept(sockfd, &client_addr, &sizevar))      
    // what if fork fails?
    if ( fork() ) {
    }
  	else {	  
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