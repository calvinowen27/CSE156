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

int main(void) {
	struct sockaddr_in serveraddr, clientaddr;
	socklen_t clientaddr_size = sizeof(clientaddr);

	int sockfd = init_socket("127.0.0.1", 99, &serveraddr);
	if (sockfd < 0) {
		logerr("main(): serverinit_socket() failed");
		exit(1);
	}

	char buf[4096];

	if (recvfrom(sockfd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&clientaddr, &clientaddr_size) < 0) {
		logerr("main(): server failed to receive from socket");
	}

	buf[4095] = 0;
	printf("%s\n", buf);

	printf("server done\n");

	close(sockfd);

	return 0;
}

// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(const char *ip_addr, int port, struct sockaddr_in *sockaddr) {
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
		fprintf(stderr, "init_socket(): failed to connect socket\n");
		return -1;
	}

	return sockfd;
}
