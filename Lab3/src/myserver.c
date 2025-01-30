#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <regex.h>
#include <arpa/inet.h>
#include "myserver.h"
#include "utils.h"

// #define IP_ADDR "127.0.0.1"
#define BUFFER_SIZE 4096
#define HEADER_SIZE 2

int main(int argc, char **argv) {
	// handle command line args
	if (argc != 2) {
		printf("Invalid number of options provided.\nThis is where I will print the usage of the program.\n");
		exit(1);
	}

	int port = atoi(argv[1]);
	if (port < 0 || port > 65535) {
		printf("Invalid port provided. Please provide a port between 0-65535.\n");
		exit(1);
	}

	// initialize server socket
	struct sockaddr_in serveraddr, clientaddr;
	socklen_t clientaddr_size = sizeof(clientaddr);

	int sockfd = init_socket(&serveraddr, NULL, port, AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
	if (sockfd < 0) {
		fprintf(stderr, "myserver ~ main(): server init_socket() failed.\n");
		exit(1);
	}

	char buf[BUFFER_SIZE + 1];
	buf[BUFFER_SIZE] = 0;

	if (echo_data(sockfd, (struct sockaddr *)&clientaddr, &clientaddr_size) < 0) {
		fprintf(stderr,"myserver ~ main(): server failed to receive from socket.\n");
	}

	close(sockfd);

	return 0;
}

// receive data from sockfd and echo it back as it arrives back to the client
// this function will run forever once called, or until there is an error (returns -1)
int echo_data(int sockfd, struct sockaddr *sockaddr, socklen_t *sock_size) {
	char buf[BUFFER_SIZE];
	memset(buf, 0, sizeof(buf));

	while (recvfrom(sockfd, buf, BUFFER_SIZE, 0, sockaddr, sock_size) >= 0) {
		if (sendto(sockfd, buf, BUFFER_SIZE, 0, sockaddr, *sock_size) < 0) {
			fprintf(stderr, "myserver ~ main(): server failed to send packets back to client.\n");
			break;
		}

		memset(buf, 0, sizeof(buf));
	}

	return -1;
}
