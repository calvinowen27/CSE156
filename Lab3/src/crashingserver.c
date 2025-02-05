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

	int sockfd = init_socket(&serveraddr, port);
	if (sockfd < 0) {
		fprintf(stderr, "crashingserver ~ main(): server init_socket() failed.\n");
		exit(1);
	}

	char buf[BUFFER_SIZE + 1];
	buf[BUFFER_SIZE] = 0;

	if (echo_data(sockfd, (struct sockaddr *)&clientaddr, &clientaddr_size) < 0) {
		fprintf(stderr,"crashingserver ~ main(): server failed to receive from socket.\n");
	}

	close(sockfd);

	return 0;
}

// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(struct sockaddr_in *sockaddr, int port) {
	// initialize socket fd
	int sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0) {
		fprintf(stderr, "crashingserver ~ init_socket(): failed to initialize socket\n");
		return -1;
	}

	// init socket address struct with ip and port
	sockaddr->sin_family = AF_INET; // IPv4
	sockaddr->sin_port = htons(port); // convert port endianness
	sockaddr->sin_addr.s_addr = INADDR_ANY;

	// bind socket
	if (bind(sockfd, (struct sockaddr *)sockaddr, sizeof(*sockaddr)) < 0) {
		fprintf(stderr, "crashingserver ~ init_socket(): failed to bind socket\n");
		fprintf(stderr, "%s\n", strerror(errno));
		return -1;
	}

	return sockfd;
}

// receive data from sockfd and echo it back as it arrives back to the client
// this function will run forever once called, or until there is an error (returns -1)
int echo_data(int sockfd, struct sockaddr *sockaddr, socklen_t *sock_size) {
	char buf[BUFFER_SIZE];
	memset(buf, 0, sizeof(buf));

	u_int8_t next_client_id = 2, serving_client_id = 0;

	int bytes_recvd, waiting_for_client = 1;
	while ((bytes_recvd = recvfrom(sockfd, buf, BUFFER_SIZE, 0, sockaddr, sock_size)) >= 0) {
		if ((u_int8_t)buf[1] == serving_client_id) {
			if (bytes_recvd > HEADER_SIZE) { // received good packet, echo back
				if (buf[0] > 2) {
					printf("crashing server crashing\n");
					exit(1);
				}
				if (sendto(sockfd, buf, BUFFER_SIZE, 0, sockaddr, *sock_size) < 0) {
					fprintf(stderr, "crashingserver ~ main(): server failed to send packets back to client.\n");
					break;
				}
			} else { // client terminated connection, serve next client
				waiting_for_client = 1;
				next_client_id = 2;
			}
		} else {
			if (waiting_for_client) {
				buf[1] = next_client_id;
				serving_client_id = next_client_id;
				waiting_for_client = 0;
				next_client_id += 1;
			} else {
				// printf("server recv packet from wrong client, turning away\n");
				buf[HEADER_SIZE] = 0;
			}

			if (sendto(sockfd, buf, BUFFER_SIZE, 0, sockaddr, *sock_size) < 0) {
				fprintf(stderr, "crashingserver ~ main(): server failed to send packets back to client.\n");
				break;
			}
		}

		memset(buf, 0, sizeof(buf));
	}

	return -1;
}
