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

#define IP_ADDR "127.0.0.1"
#define BUFFER_SIZE 4096

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

	int sockfd = init_socket(&serveraddr, IP_ADDR, port);
	if (sockfd < 0) {
		fprintf(stderr, "myserver ~ main(): server init_socket() failed.\n");
		exit(1);
	}

	char buf[BUFFER_SIZE + 1];
	buf[BUFFER_SIZE] = 0;

	while (recvfrom(sockfd, buf, BUFFER_SIZE, 0, (struct sockaddr *)&clientaddr, &clientaddr_size) > 0) {
		printf("%d %d %d %d\n", buf[0], buf[1], buf[2], buf[3]);
		printf("%s\n\n", buf+4);

		if (sendto(sockfd, buf, BUFFER_SIZE, 0, (struct sockaddr *)&clientaddr, clientaddr_size) < 0) {
			fprintf(stderr, "myserver ~ main(): server failed to send packets back to client.\n");
		}
	}

	fprintf(stderr,"myserver ~ main(): server failed to receive from socket.\n");

	printf("myserver ~ done\n");

	close(sockfd);

	return 0;
}

// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(struct sockaddr_in *sockaddr, const char *ip_addr, int port) {
	// initialize socket fd
	int sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0) {
		fprintf(stderr, "init_socket(): failed to initialize socket\n");
		return -1;
	}

	// init socket address struct with ip and port
	// struct sockaddr_in sockaddr;

	sockaddr->sin_family = AF_INET; // IPv4
	sockaddr->sin_port = htons(port); // convert port endianness
	sockaddr->sin_addr.s_addr = inet_addr(ip_addr);

	// bind socket
	if (bind(sockfd, (struct sockaddr *)sockaddr, sizeof(*sockaddr)) < 0) {
		fprintf(stderr, "init_socket(): failed to bind socket\n");
		fprintf(stderr, "%s\n", strerror(errno));
		return -1;
	}

	return sockfd;
}
