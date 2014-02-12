/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <string>
#include "http-request.h"
using namespace std;

const char* PORT_PROXY_LISTEN = "14886";
const int BUFSIZE = 1024;
const int BACKLOG = 20;
#define CHECK(F) if ( (F) == -1 ) cerr << "Error when calling " << #F << endl;
#define CHECK_CONTINUE(F) if ( (F) == -1 ) { cerr << "Error when calling " << #F << endl; continue; }
#define ERROR(format, ...) fprintf(stderr, format, ## __VA_ARGS__);
#define NOFLAGS 0

int processClient(int clientfd) {
  // FUTURE NOTE: for persistent connections we may want to wrap in a loop
  // read request from client
  string tmp_req;
  char buf[BUFSIZE];
  int len;
  while(memmem(tmp_req.c_str(), tmp_req.length(), "/r/n/r/n", 4) == NULL) {
    cerr << "Looking for carriage return ... " << endl;
    memset(&buf, 0, sizeof buf);
    len = read(clientfd, buf, sizeof buf);
    if (len > 0) {
      tmp_req.append(buf);
      cerr << "Read " << len << " bytes" << endl;
    } 
    else {
      cerr << "Didn't read anything" << endl;
      break;
    }
    cerr << "TEMP: " << tmp_req << endl;
  }
  
  // parse the client's request
  HttpRequest req;  
  try {
    req.ParseRequest(tmp_req.c_str(), tmp_req.length());
    
    // open connection with remote server
    
  } catch (ParseException e) {
    cerr << e.what() << endl;
    string response;
    string get_err = "Request is not GET";
    
    // 
    if (strcmp(e.what(), get_err.c_str()) == 0)
      response = "501 Not Implemented\r\n\r\n";
    else
      response = "400 Bad Request\r\n\r\n";
    
    write(clientfd, response.c_str(), response.length());
  }
  
  cerr << "All done here" << endl;
  close(clientfd);
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

int createRemoteSocket(string host, short port) {
  // struct addrinfo hints, *server_addr;
  // memset(&hints, 0, sizeof hints); // clear struct
  // hints.ai_family   = AF_INET;     // handle IPv4
  // hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  // int status;
  // if ( (status = getaddrinfo(host.c_str(), to_string(port).c_str(), &hints, &server_addr)) != 0 )
  //   ERROR("Invalid remote server %s:%d", host.c_str(), port);
  // 
  // //Get a file descriptor that we can use to write to the server
  // int server_fd;
  // CHECK(server_fd = socket(server_addr->ai_family, server_addr->ai_socktype, server_addr->ai_protocol))
  // //Now establish a connection with the server at the port the client asked for.
  // CHECK(connect(server_fd, server_addr->ai_addr, server_addr->ai_addrlen))
  // //We've connected the socket to the remote address. Now it's ready to talk to.
  // return server_fd;
  return -1;
}

void tellClientUnsupportedMethod(int client_fd) {
	// A simple helper function to tell the client that they asked for an unsupported method.
	//TODO: fill in this function with an actual http response with the error code
	ERROR("Unsupported method");
}

void deliverPage(int client_fd) {
	/* This function simply parses the client's request for a server and web page request,
	then fetches it from the remote server and delivers it to the client.
	Input: client socket file descriptor */
	//First read the request from the client
	char read_buffer[BUFSIZE];
	int request_length;
	CHECK(request_length = recv(client_fd, read_buffer, sizeof read_buffer, NOFLAGS))
	//Put the client's request into an HttpRequest object to parse it
	HttpRequest client_request;
	client_request.ParseRequest(read_buffer, request_length);
	//Do some simple evaluation. If the method is unsupported, call a function to tell the client
	if ( client_request.GetMethod() == HttpRequest::UNSUPPORTED )
		tellClientUnsupportedMethod(client_fd);
	//If the request is totally valid, find out what server we need to talk to.
	int server_fd = createRemoteSocket(client_request.GetHost(), client_request.GetPort());
	//Request the page from the server, storing it somewhere temporarily
		//Can we just send the contents of read_buffer to the server, since that's the request?
		//TODO: If so, change the name of read_buffer to request_buffer
	CHECK(send(server_fd, read_buffer, request_length, NOFLAGS))
	//Fetch the page from the server into a temporary buffer
	char response_buffer[BUFSIZE];
	int response_length;
	CHECK(response_length = recv(server_fd, response_buffer, sizeof response_buffer, NOFLAGS))
	//Now send it to the client
	CHECK(send(client_fd, response_buffer, response_length, NOFLAGS))
	//We're done. Don't close the connection; let the main loop decide if that should be done.
}

int main (int argc, char *argv[]) {
  // create listen socket
	int sockfd = createListenSocket();
  
  // first start listening for connections
  CHECK(listen(sockfd, BACKLOG))
    
  // now loop forever, accepting a connection and forking a new process to deal with it
  for (;;) {
    // setup client addr info
    struct sockaddr client_addr;
    memset(&client_addr, 0, sizeof client_addr);
    socklen_t sizevar = (socklen_t) sizeof client_addr;
    int client_fd;
    
    // accept client connection
  	CHECK_CONTINUE(client_fd = accept(sockfd, &client_addr, &sizevar))
      
    // what if fork fails?
    if ( fork() ) //in parent 
      // need to track how many children have been forked
      close(client_fd); //don't need child's connection
  	else {
      // close(sockfd); //don't need parent's connection
      // CHECK(send(client_fd, "Why hello there!", 16, 0))
      // deliverPage(client_fd);
      processClient(client_fd);
      close(client_fd); //Done with client
  		exit(0);
  	}
      
    // if max # of processes have been forked
    // then we should wait for any process to 
    // finish before continuing
  }
  return 0;
}