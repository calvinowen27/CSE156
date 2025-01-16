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
	(void)outfile_path;
	
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

// split uint32_t into uint8_t[4]
uint8_t *split_bytes(uint32_t val) {
	uint8_t *res = calloc(4, sizeof(uint8_t *));

	res[0] = val & 0xff000000;
	res[1] = val & 0x00ff0000;
	res[2] = val & 0x0000ff00;
	res[3] = val & 0x000000ff;

	return res;
}

// send file from fd to sockfd, also using sockaddr
// return 0 on success, -1 on error
int send_file(int fd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mtu) {
	char buf[mtu];

	uint32_t packet_num = 0;

	int bytes_read;
	do {
		bytes_read = read(fd, buf + 4, sizeof(buf) - 4);

		// assign packet id to first 4 bytes of packet
		uint8_t *pn_bytes = split_bytes(packet_num);
		buf[0] = pn_bytes[0]+1;
		buf[1] = pn_bytes[1]+1;
		buf[2] = pn_bytes[2]+1;
		buf[3] = pn_bytes[3]+1;

		if (sendto(sockfd, buf, bytes_read + 4, 0, sockaddr, sockaddr_size) < 0) {
			fprintf(stderr, "myclient ~ send_file(): client failed to send packetto server.\n");
			return -1;
		}

		packet_num += 1;
	} while (bytes_read > 0);

	return 0;
}
