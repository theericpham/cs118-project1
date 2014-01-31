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
using namespace std;

const char* PORT_PROXY_LISTEN = "14866";
const int BACKLOG = 20;
#define CHECK(F) if ( F == -1 ) cerr << "Error when calling " << #F << endl;
#define CHECK_CONTINUE(F) if ( F == -1 ) { cerr << "Error when calling " << #F << endl; continue; }

int createServerSocket() {
	/* This function returns a file descriptor that points to a new socket bound to 
	localhost:PORT_PROXY_LISTEN. It will be ready to listen for and accept TCP connections. */
	
  // generate addresses which can bind to a socket for client requests
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof hints); // clear struct
  hints.ai_family   = AF_INET;     // handle IPv4
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  hints.ai_flags    = AI_PASSIVE;  // assign my localhost addr to sockets
  
  int status;
  if ((status = getaddrinfo(NULL, PORT_PROXY_LISTEN, &hints, &res)) != 0)
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));

  // res now points to a linked list of struct addrinfos, probably just 1 in this case
  // create a socket
  int sockfd;
  CHECK((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)))
  //Now bind the socket to the address we got for ourselves earlier
  CHECK(bind(sockfd, res->ai_addr, res->ai_addrlen))
  return sockfd;
}

int main (int argc, char *argv[])
{
	int sockfd = createServerSocket();
  //First start listening for connections
  CHECK(listen(sockfd, BACKLOG))
  //Now loop forever, accepting a connection and forking a new process to deal with it
  struct sockaddr client_addr;
  int client_fd;
  socklen_t sizevar;
  for (;;) {
  	sizevar = (socklen_t)sizeof client_addr;
  	CHECK_CONTINUE((client_fd = accept(sockfd, &client_addr, &sizevar)))
  	if ( fork() ) //in parent
  		close(client_fd); //don't need child's connection
  	else {
  		close(sockfd); //don't need parent's connection
  		CHECK(send(client_fd, "Why hello there!", 14, 0))
  		close(client_fd);
  		exit(0);
  		}
  	}
  return 0;
}