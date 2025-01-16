#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "myclient.h"
#include "utils.h"

int main(int argc, char **argv) {
	// handle command line args
	if (argc != 6) {
		printf("Invalid number of options provided.\nThis is where I will print the usage of the program.\n");
		exit(1);
	}

	char *server_ip = argv[1];																// server ip
	int server_port = atoi(argv[2]);														// server port
	if (server_port < 0 || server_port > 65535) {
		printf("Invalid port provided. Please provide a server port between 0-65535.\n");
		exit(1);
	}

	int mtu = atoi(argv[3]);																// mtu
	if (mtu < 0) {
		printf("Invalid mtu provided. Please provide a positive integer as the mtu.\n");
		exit(1);
	}

	char *infile_path = argv[4];															// infile path
	char *outfile_path = argv[5];															// outfile path
	
	int infd = open(infile_path, 0600);
	if (infd < 0) {
		fprintf(stderr, "myclient ~ main(): failed to open file %s\n", infile_path);
	}

	// initialize socket
	struct sockaddr_in serveraddr;
	socklen_t serveraddr_size = sizeof(serveraddr);
	int sockfd = init_socket(&serveraddr, server_ip, server_port);
	if (sockfd < 0) {
		fprintf(stderr, "myclient ~ main(): failed to initialize socket.\n");
		exit(1);
	}

	// send in file to server
	if (send_file(infd, sockfd, (struct sockaddr *)&serveraddr, serveraddr_size, mtu) < 0) {
		fprintf(stderr, "myclient ~ main(): failed to send file %s to server.\n", infile_path);
		exit(1);
	}

	// receive file echo from server

	printf("client done\n");

	close(sockfd);
	close(infd);

	return 0;
}

// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(struct sockaddr_in *sockaddr, const char *ip_addr, int port) {
	int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0) {
		fprintf(stderr, "myclient ~ init_socket(): client failed to initialize socket.\n");
		return -1;
	}

	sockaddr->sin_family = AF_INET;
	sockaddr->sin_port = htons(port);
	sockaddr->sin_addr.s_addr = inet_addr(ip_addr);

	return sockfd;
}

// send file from fd to sockfd, also using sockaddr
// return 0 on success, -1 on error
int send_file(int fd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mtu) {
	char buf[mtu];

	int bytes_read;
	while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
		if (sendto(sockfd, buf, bytes_read, 0, sockaddr, sockaddr_size) < 0) {
			fprintf(stderr, "myclient ~ send_file(): client failed to send packetto server.\n");
			return -1;
		}
	}

	return 0;
}
