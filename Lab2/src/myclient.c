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
#define MIN_MSS_SIZE 5
#define WINDOW_SIZE 10 // must be < 256 unless packet ID byte count increases

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

	int mss = atoi(argv[3]);																// mss
	if (mss < 0) {
		printf("Invalid mss provided. Please provide a positive integer as the mss.\n");
		exit(1);
	}

	// check for valid mss size
	if (mss < MIN_MSS_SIZE) {
		printf("Required minimum mss is 5.\n");
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
	if (send_recv_file(infd, outfd, sockfd, (struct sockaddr *)&serveraddr, serveraddr_size, mss) < 0) {
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
int send_recv_file(int infd, int outfd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mss) {
	char buf[mss];
	memset(buf, 0, sizeof(buf));

	uint8_t packet_id_recvd;

	fd_set fds;
	FD_SET(sockfd, &fds);

	struct timeval timeout;
	timeout.tv_sec = 60; // 60s timeout for recvfrom server

	/*
			- allocate pairs of packets received to place in file (if OOO)
			- send window packets
			- wait for all window packets to come back
				- if timeout encountered on wait, packet loss
				- if packet received before expected, put packets between last received and this one into pair with
				  packet ID and file seek index (might need to leave space for multiple?)
				- if packet received after expected, find location from pair using packet ID and insert into file,
				  then update OOO file location for any subsequent packets waiting to be received
	 */

	uint32_t ooo_packet_ids[WINDOW_SIZE];
	off_t ooo_packet_locations[WINDOW_SIZE];
	int num_packets_not_recvd = 0; // number of packets still out (data in use in above arrays)
	int i;
	for (i = 0; i < WINDOW_SIZE; i++) {
		ooo_packet_ids[i] = 0;
		ooo_packet_locations[i] = 0;
	}

	int bytes_read = 1, bytes_recvd, last_packet_id_expected = 1;

	uint8_t packet_id_sent, expected_packet_id_recv;
	int packets_recvd = 0;

	while (bytes_read > 0) { // continue until no more bytes read from file
		for (packet_id_sent = 1; packet_id_sent <= WINDOW_SIZE; packet_id_sent++) {
			bytes_read = read(infd, buf + 1, sizeof(buf) - 1);
			if (bytes_read < 0) {
				fprintf(stderr, "myclient ~ send_recv_file(): encountered an error reading from infile.\n");
				return -1;
			} else if (bytes_read == 0) {
				break; // finished reading file, now just wait to receive rest of packets
			}
			
			// assign packet ID
			buf[0] = packet_id_sent;

			if (sendto(sockfd, buf, bytes_read + 1, 0, sockaddr, sockaddr_size) < 0) {
				fprintf(stderr, "myclient ~ send_recv_file(): client failed to send packet to server.\n");
				return -1;
			}

			last_packet_id_expected = packet_id_sent;

			memset(buf, 0, sizeof(buf));
		}

		expected_packet_id_recv = 1;
		packets_recvd = 0;
		while (packets_recvd < last_packet_id_expected) {
			if (select(sockfd + 1, &fds, NULL, NULL, &timeout)) { // check there is data to be read from socket
				if ((bytes_recvd = recvfrom(sockfd, buf, sizeof(buf), 0, sockaddr, &sockaddr_size)) < 0) {
					fprintf(stderr, "myclient ~ send_recv_file(): an error occured while receiving data from server.\n");
					fprintf(stderr, "%s\n", strerror(errno));
					return -1;
				} else { // recvfrom succeeds
					packet_id_recvd = (uint8_t)buf[0];
					if (packet_id_recvd == expected_packet_id_recv) {
						// write bytes to outfile
						if (write_n_bytes(outfd, buf + 1, bytes_recvd - 1) < 0) {
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
						if (write_n_bytes(outfd, buf + 1, bytes_recvd - 1) < 0) {
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
						for (i = 0; i < num_packets_not_recvd; i++) {
							if (ooo_packet_ids[i] == packet_id_recvd) {
								// go to correct location in outfile
								lseek(outfd, ooo_packet_locations[i], SEEK_SET);
								// write bytes to outfile
								if (write_n_bytes(outfd, buf + 1, bytes_recvd - 1) < 0) {
									fprintf(stderr, "myclient ~ send_recv_file(): encountered error writing bytes to outfile\n");
									fprintf(stderr, "%s\n", strerror(errno));
									return -1;
								}
								
								// shift packet info down in buffer
								if (i < num_packets_not_recvd) {
									ooo_packet_ids[i] = ooo_packet_ids[i+1];
									ooo_packet_locations[i] = lseek(outfd, 0, SEEK_CUR);
								}
							} else if (ooo_packet_ids[i] > packet_id_recvd) {
								// shift packet info down in buffer
								if (i < num_packets_not_recvd) {
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
			} else { // after 60s timeout
				// if haven't received more packets than ooo packets, can't detect server
				// otherwise we've timed out waiting for a dropped packet
				if (last_packet_id_expected - packets_recvd > num_packets_not_recvd) {
					fprintf(stderr, "Cannot detect server.\n");
				} else {
					fprintf(stderr, "Packet loss detected.\n");
				}
				return -1;
			}

			packets_recvd += 1;

			memset(buf, 0, sizeof(buf));
		}
	}

	return 0;
}
