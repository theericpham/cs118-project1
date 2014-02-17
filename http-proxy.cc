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

const string GET_ERROR						= "Request is not GET";
const string NOT_IMPLEMENTED_MSG	= "Not Implemented";
const string BAD_REQUEST_MSG			= "Bad Request";
const string NOT_IMPLEMENTED_CODE = "501";
const string BAD_REQUEST_CODE		 = "400";
const char* NON_PERSISTENT				= "1.0";
const char* PERSISTENT						= "1.1";

const char* PORT_PROXY_LISTEN		 = "14886";
const short PORT_SERVER_DEFAULT	 = 80;
const int BUFSIZE								 = 1024;
const int BACKLOG								 = 20;

#define CHECK(F) if ( (F) == -1 ) cerr << "Error when calling " << #F << endl;
#define CHECK_CONTINUE(F) if ( (F) == -1 ) { cerr << "Error when calling " << #F << endl; continue; }
#define ERROR(format, ...) fprintf(stderr, format, ## __VA_ARGS__);
#define NOFLAGS 0

void clean_up_socket(int fd) {
	close(fd);
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

string readFromSocket(int fd) {
	//A generic function that simply reads from any socket file into a C++ string
	// Transfer data one KB at a time from C string buffer to C++ string result.
	char buffer[BUFSIZE + 1];
	string result;
	int len;
	do {
		memset(&buffer, 0, sizeof buffer);
		len = read(fd, buffer, sizeof buffer);
		result.append(buffer);
	} while ((len > 0) && (memmem(result.c_str(), result.length(), "\r\n\r\n", 4) == NULL));
	return result;
}


HttpResponse readServerResponse(int server_fd) {
	//Create an empty HttpResponse object. Call our generic low-level function to read from 
	//the server socket into a C++ string, then push that into the HttpResponse as a C string
	//and return the HttpResponse object.
	//Once again, literally the same thing as readClientRequest, except we have to call different
	//functions here.
	HttpResponse response;
	string response_string = readFromSocket(server_fd);
	//Now all of server response is in response_string; load it into an HttpResponse object
	response.ParseResponse(response_string.c_str(), response_string.length());
	return response;
}

HttpRequest readClientRequest(int client_fd) {
	//Returns HttpRequest object if succesful, throws ParseException up to next level if fails
	string request_string = readFromSocket(client_fd);
	cerr << "Finished Reading Client Request:" << endl << request_string << endl;
	
	//Now create a HttpRequest object to parse the client's request
	//This one is a little more complicated than readServerResponse because we have to check
	//for errors.
	HttpRequest request;
	try {
		request.ParseRequest(request_string.c_str(), request_string.length());
	} catch (ParseException e) {
		throw; //Make caller deal with this, because it'll need to tell the client the request was bad.
	}
	return request;
}

int sendResponseToClient(int client_fd, const HttpResponse& response) {
	//This function simply flattens an HttpResponse object into a C string and sends it.
	char response_buffer[response.GetTotalLength()];
	response.FormatResponse(response_buffer);
	return write(client_fd, response_buffer, sizeof response_buffer);
}

int sendRequestToServer(int server_fd, const HttpRequest& request) {
	//This function simply flattens an HttpRequest object into a C string and sends it.
	//Note that this function is literally the exact same thing as sendResponseToClient,
	//except we have to use the different functions FormatRequest and FormatResponse,
	//which are not virtual.. this is really icky OO for class HttpHeaders :(
	char request_buffer[request.GetTotalLength()];
	request.FormatRequest(request_buffer);
	cerr << "Formatted Proxy Request: " << endl << request_buffer << endl;
	return write(server_fd, request_buffer, sizeof request_buffer);
}

int processClient(int client_fd) {
	HttpResponse proxy_response; //Will be used to store both success and failure response
	//First try to read the client's request. If there's a problem, form an error response.
	try {
		HttpRequest client_request = readClientRequest(client_fd);
	
		//Get the page and store the current time and expiration date. If there's no expiration date,
		//just set it to some special value.
		//Then when the client says "I want www.google.com/" look it up in the map and check the 
		//expired time. If it's expired, then ask the server if it's been modified. If it has, 
		//get a new copy to put in our $. 
		//No matter, send the client our $ed copy in the end, cus it will have been updated if needed.
		
		// connect to remote server and return file descriptor	
		string remote_server_host = (client_request.GetHost().length() != 0) ? client_request.GetHost() : client_request.FindHeader("Host");
		short remote_server_port = (client_request.GetPort() > 0) ? client_request.GetPort() : PORT_SERVER_DEFAULT;
		int server_fd = createRemoteSocket(remote_server_host, remote_server_port);
	
		// foward client request to remote server
		CHECK(sendRequestToServer(server_fd, client_request))
		//And get back the response to forward to client a bit later
		proxy_response = readServerResponse(server_fd);
		// close and shutdown connection with remote server
		clean_up_socket(server_fd);
	} //end try
	catch (ParseException e) {
		//TODO what if there's an error parsing the server response? :o
		//Use proxy_response as an error response that will be sent to the client instead of the requested page
		cerr << "Error Parsing Client Request: " << e.what() << endl;
		proxy_response.SetVersion(NON_PERSISTENT);
		bool not_impl = !strcmp(e.what(), GET_ERROR.c_str());
		proxy_response.SetStatusMsg(not_impl ? NOT_IMPLEMENTED_MSG : BAD_REQUEST_MSG);
		proxy_response.SetStatusCode(not_impl ? NOT_IMPLEMENTED_CODE : BAD_REQUEST_CODE);
		cerr << "Returned Error Response to Client" << endl;
	}
	
	//These things have to be done whether or not the parsing succeeded
	// return server response to client
	CHECK(sendResponseToClient(client_fd, proxy_response))
		 
	// close and shutdown connection with client
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
