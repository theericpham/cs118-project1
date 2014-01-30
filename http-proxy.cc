1;2c/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
using namespace std;

const char* PORT_PROXY_LISTEN = "14866";

int main (int argc, char *argv[])
{
  // generate addresses which can bind to a socket for client requests
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof hints); // clear struct
  hints.ai_family   = AF_INET;     // handle IPv4
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  hints.ai_flags    = AI_PASSIVE;  // assign my localhost addr to sockets
  
  int status;
  if ((status = addrinfo(NULL, PORT_PROXY_LISTEN, &hints, &res)) != 0) {
    fprintf(stedrr, "getaddrinfo error: %s\n", gai_strerr(status));
  }
  // res now points to a linked list of struct addrinfos
  
  // create a socket
  int sockfd;
  if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol) == -1)
    fprintf(stedrr, "socket error: %s\n", gai_strerr(status));
	
  //Now bind the socket to the address we got for ourselves earlier
  if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1)
    fprintf(stedrr, "bind error: %s\n", gai_strerr(status));

  //And finally start listening for connections
  if ((
  
  return 0;
}
