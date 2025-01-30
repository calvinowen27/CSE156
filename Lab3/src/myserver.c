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
#include "protocol.h"

// #define IP_ADDR "127.0.0.1"
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

// TODO: rework for server side (copied from myclient Lab2)
void recv_data() {
	packet_id_recvd = (uint8_t)buf[0];
	if (packet_id_recvd == expected_packet_id_recv) {
		// write bytes to outfile
		if (write_n_bytes(outfd, buf+HEADER_SIZE, strlen(buf+HEADER_SIZE)) < 0) {
			fprintf(stderr, "myclient ~ send_recv_file(): encountered error writing bytes to outfile\n");
			fprintf(stderr, "%s\n", strerror(errno));
			return -1;
		}

		// normal, increment expected packet ID
		expected_packet_id_recv = packet_id_recvd + 1;
	} else if (packet_id_recvd > expected_packet_id_recv) { // packet recvd too soon
		// store all packets in between recvd and expected into ooo buffer with file locations
		for (uint8_t id = expected_packet_id_recv; id < packet_id_recvd; id++) {
			ooo_packet_ids[num_packets_not_recvd] = id;
			ooo_packet_locations[num_packets_not_recvd] = lseek(outfd, 0, SEEK_END);
			num_packets_not_recvd += 1;
		}

		// write bytes to outfile
		if (write_n_bytes(outfd, buf+HEADER_SIZE, strlen(buf+HEADER_SIZE)) < 0) {
			fprintf(stderr, "myclient ~ send_recv_file(): encountered error writing bytes to outfile\n");
			fprintf(stderr, "%s\n", strerror(errno));
			return -1;
		}

		// increment expected packet ID
		expected_packet_id_recv = packet_id_recvd + 1;
	} else { // packet recvd late
		// write packet data to file depending on ooo buffer file location
		// pop packet data from ooo buffers
		// change all ooo packet file locations with larger ID to current position of seek ptr
		for (int i = 0; i < num_packets_not_recvd; i++) {
			if (ooo_packet_ids[i] == packet_id_recvd) {
				// go to correct location in outfile
				off_t file_idx = lseek(outfd, ooo_packet_locations[i], SEEK_SET);

				if (shift_file_contents(outfd, file_idx, strlen(buf+HEADER_SIZE)) < 0) {
					fprintf(stderr, "myclient ~ send_recv_file(): encountered error shifting file contents for OOO write.\n");
					return -1;
				}

				// write bytes to outfile
				if (write_n_bytes(outfd, buf+HEADER_SIZE, strlen(buf+HEADER_SIZE)) < 0) {
					fprintf(stderr, "myclient ~ send_recv_file(): encountered error writing bytes to outfile\n");
					fprintf(stderr, "%s\n", strerror(errno));
					return -1;
				}
				
				// shift packet info down in buffer
				if (i < num_packets_not_recvd - 1) {
					ooo_packet_ids[i] = ooo_packet_ids[i+1];
					ooo_packet_locations[i] = lseek(outfd, 0, SEEK_CUR);
				} else {
					ooo_packet_ids[i] = 0;
					ooo_packet_locations[i] = 0;
				}
			} else if (ooo_packet_ids[i] > packet_id_recvd) {
				// shift packet info down in buffer
				if (i < num_packets_not_recvd - 1) {
					ooo_packet_ids[i] = ooo_packet_ids[i+1];
					ooo_packet_locations[i] = lseek(outfd, 0, SEEK_CUR);
				} else {
					ooo_packet_ids[i] = 0;
					ooo_packet_locations[i] = 0;
					num_packets_not_recvd -= 1;
				}
			}
		}
		
		lseek(outfd, 0, SEEK_END); // reset seek ptr in case more packets come in

		// expected not recvd yet, leave value the same
	}
}
