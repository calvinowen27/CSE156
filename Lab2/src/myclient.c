#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include "myclient.h"
#include "utils.h"

#define BUFFER_SIZE 4096
#define MIN_MTU_SIZE 5

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

	// check for valid mtu size
	if (mtu < MIN_MTU_SIZE) {
		printf("Required minimum MTU is 5.\n");
		exit(1);
	}

	char *infile_path = argv[4];															// infile path
	char *outfile_path = argv[5];															// outfile path
	(void)outfile_path;
	
	int infd = open(infile_path, 0600);
	if (infd < 0) {
		fprintf(stderr, "myclient ~ main(): failed to open file %s\n", infile_path);
	}

	int outfd = creat(outfile_path, 0664);
	if (outfd < 0) {
		fprintf(stderr, "myclient ~ main(): failed to open/create outfile %s\n", outfile_path);
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
	if (send_recv_file(infd, outfd, sockfd, (struct sockaddr *)&serveraddr, serveraddr_size, mtu) < 0) {
		fprintf(stderr, "myclient ~ main(): failed to send or receive file %s to server.\n", infile_path);
		exit(1);
	}

	close(sockfd);
	close(infd);
	close(outfd);

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
int send_recv_file(int infd, int outfd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mtu) {
	char buf[mtu];
	memset(buf, 0, sizeof(buf));

	uint32_t packet_num_sent, packet_num_recvd, new_packet_num;
	packet_num_sent = 0;
	packet_num_recvd = -1;

	fd_set fds;
	FD_SET(sockfd, &fds);

	struct timeval timeout;
	timeout.tv_sec = 60; // 60s timeout for recevfrom server

	int bytes_read, bytes_recvd;
	while ((bytes_read = read(infd, buf + 4, sizeof(buf) - 4)) > 0) {
		// assign packet id to first 4 bytes of packet
		uint8_t *pn_bytes = split_bytes(packet_num_sent);
		buf[0] = pn_bytes[0]+1;
		buf[1] = pn_bytes[1]+1;
		buf[2] = pn_bytes[2]+1;
		buf[3] = pn_bytes[3]+1;

		if (sendto(sockfd, buf, bytes_read + 4, 0, sockaddr, sockaddr_size) < 0) {
			fprintf(stderr, "myclient ~ send_recv_file(): client failed to send packet to server.\n");
			return -1;
		}

		if (select(sockfd + 1, &fds, NULL, NULL, &timeout)) {
			if ((bytes_recvd = recvfrom(sockfd, buf, sizeof(buf), 0, sockaddr, &sockaddr_size)) < 0) {
				fprintf(stderr, "Cannot detect server\n");
				return -1;
			} else {
				buf[0] -= 1;
				buf[1] -= 1;
				buf[2] -= 1;
				buf[3] -= 1;
				new_packet_num = reunite_bytes((uint8_t *)buf);
				if (new_packet_num != packet_num_recvd + 1) {
					fprintf(stderr, "Packet loss detected\n");
					exit(1);
				} else {
					packet_num_recvd = new_packet_num;
					// write bytes to outfile
					if (write_n_bytes(outfd, buf + 4, bytes_recvd - 4) < 0) {
						fprintf(stderr, "myclient ~ send_recv_file(): encountered error writing bytes to outfile\n");
						fprintf(stderr, "%s\n", strerror(errno));
						return -1;
					}
				}
			}
		} else { // after 60s timeout
			fprintf(stderr, "Cannot detect server\n");
			return -1;
		}

		packet_num_sent += 1;

		memset(buf, 0, sizeof(buf));
	}

	return 0;
}
