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
#include "http-response.h"
using namespace std;

const string GET_ERROR            = "Request is not GET";
const string NOT_IMPLEMENTED_MSG  = "Not Implemented";
const string BAD_REQUEST_MSG      = "Bad Request";
const string NOT_IMPLEMENTED_CODE = "501";
const string BAD_REQUEST_CODE     = "400";
const char* NON_PERSISTENT        = "1.0";
const char* PERSISTENT            = "1.1";

const char* PORT_PROXY_LISTEN     = "14886";
const short PORT_SERVER_DEFAULT   = 80;
const int BUFSIZE                 = 1024;
const int BACKLOG                 = 20;

#define CHECK(F) if ( (F) == -1 ) cerr << "Error when calling " << #F << endl;
#define CHECK_CONTINUE(F) if ( (F) == -1 ) { cerr << "Error when calling " << #F << endl; continue; }
#define ERROR(format, ...) fprintf(stderr, format, ## __VA_ARGS__);
#define NOFLAGS 0


int createRemoteSocket(string host, short port) {
	cerr << "Connecting to " << host << ":" << port << endl;
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

int processClient(int client_fd) {
  cerr << "Processing Client" << endl;

  string request, response;
  char client_request_buffer[BUFSIZE + 1];
  int len;
  do {
    memset(&client_request_buffer, 0, sizeof client_request_buffer);
    len = read(client_fd, client_request_buffer, sizeof client_request_buffer);
    request.append(client_request_buffer);
  } while ((len > 0) && (memmem(request.c_str(), request.length(), "\r\n\r\n", 4) == NULL));
  // cerr << "Client Request: " << endl << tmp_req << endl;
  cerr << "Finished Reading Client Request: " << endl << request << endl;
  
  // parse client request
  HttpRequest client_request;
  try {
    client_request.ParseRequest(request.c_str(), request.length());
  } catch (ParseException e) {
    cerr << "Error Parsing Client Request: " << e.what() << endl;
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
    
    len = error_response.GetTotalLength() + 1;
    char response_buffer[len];
    error_response.FormatResponse(response_buffer);
    write(client_fd, response_buffer, len);
    close(client_fd);
    cerr << "Returned Error Response to Client and Closing Connection" << endl;
  }
  
  char proxy_request_buffer[client_request.GetTotalLength() + 1];
  client_request.FormatRequest(proxy_request_buffer);
  cerr << "Formatted Proxy Request: " << endl << proxy_request_buffer << endl;
  if (memmem(proxy_request_buffer, client_request.GetTotalLength() + 1, "\r\n\r\n", 4))
    cerr << "Proxy Request Contains \\r\\n" << endl;
  else
    cerr << "Proxy Request Missing \\r\\n" << endl;
  
  string remote_server_host;
  short  remote_server_port;
  remote_server_host = (client_request.GetHost().length() == 0) ? client_request.GetHost() : client_request.FindHeader("Host");
  remote_server_port = (!client_request.GetPort()) ? client_request.GetPort() : PORT_SERVER_DEFAULT;
  
  cerr << "Client Wants to Connect to Remote Server Host " << remote_server_host << " on port " << remote_server_port << endl;
  // 
  // //Connect to the remote server that the client requested and return the file descriptor
  //   int server_fd = createRemoteSocket(host, port);
  //   CHECK(send(server_fd, proxy_request, client_request.GetTotalLength(), NOFLAGS))
  
 //Now we can forward the client's request to the server.
 //First re-format the request to a string
 // char send_buffer[BUFSIZE];
 // char* send_buffer_end = client_request.FormatRequest(send_buffer);
 // int send_buffer_length = send_buffer_end - send_buffer;
 // //Then send the request to the server 
 // CHECK(send(server_fd, send_buffer, send_buffer_length, NOFLAGS))
   
 
 // CHECK(write(server_fd, proxy_request, request_length))
 //   cerr << "Sent Request to Server:" << endl << proxy_request << endl;
 
 // HttpResponse server_res;
 // long total_length;
 // string tmp_res;
 // do {
 //   memset(&buf, 0, sizeof buf);
 //   len = read(server_fd, buf, sizeof buf);
 //   tmp_res.append(buf);
 //   
 //   server_res.ParseResponse(tmp_res.c_str(), tmp_res.length());
 //   total_length = server_res.GetTotalLength() + stol(server_res.FindHeader("Content-Length"));
 // } while (len > 0 && tmp_res.length() < total_length);
 // close(server_fd);
 // cerr << "Closed Connection with Server ... Server Response: " << endl << tmp_res << endl;
 // // tmp_res.append("\r\n\r\n");
 // char response[total_length];
 // server_res.FormatResponse(response);
 // write(client_fd, response, total_length);
 // cerr << "Sent Response to Client" << endl;
 return -1;
    
  //   // FUTURE NOTE: our connection to the remote server is HTTP/1.1
  //   //              so we should have a timer to close the connection
  //   // read server response
  //   HttpResponse server_response;
  //   long total_length = -1;
  //   string tmp_response;
  //   do {
  //     memset(&buf, 0, sizeof buf);
  //     len = read(server_fd, buf, sizeof buf);
  //     cerr << "The server returned " << len << "bytes: " << endl << buf << endl;
  //     tmp_response.append(buf);
  //     
  //     if (total_length == -1) {
  //       server_response.ParseResponse(tmp_response.c_str(), tmp_response.length());
  //       total_length = server_response.GetTotalLength() + stol(server_response.FindHeader("Content-Length"));
  //     }
  // 
  //     cerr << "Total Response Data Retrieved: " << tmp_response.length() << " / " << total_length << " bytes." << endl;
  //   } while (len > 0 && tmp_response.length() < total_length);
  //   close(server_fd);
  //   cerr << "Closing Connection with Server ... Server Responded: " << endl << tmp_response << endl;
  // 
  // //Now send it to the client
  //   // CHECK(send(client_fd, response_buffer, response_length, NOFLAGS))
  //   write(client_fd, tmp_response.c_str(), tmp_response.length());
  //   
  //   cerr << "Finished Processing Client Request(s)" << endl;
  //   close(client_fd);
  //   return -1;
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
  for (;;) {
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
