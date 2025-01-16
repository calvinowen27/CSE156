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


	if (sendto(sockfd, "1hello1", 8, 0, (struct sockaddr *)&serveraddr, serveraddr_size) < 0) {
		fprintf(stderr, "myclient ~ main(): client failed to send message to server.\n");
	}

	char buf[4096];
	buf[4095] = 0;

	if (recvfrom(sockfd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&serveraddr, &serveraddr_size) < 0) {
		fprintf(stderr, "myclient ~ main(): something went wrong with receiving packets from server.\n");
	} else {
		printf("received:\n%s\n\n", buf);
	}

	printf("client done\n");

	close(sockfd);
	close(infd);

	return 0;
}

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
