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
#include <sys/stat.h>
#include <regex.h>
#include "myclient.h"
#include "utils.h"

#define BUFFER_SIZE 4096
#define HEADER_SIZE 2
#define MIN_MSS_SIZE HEADER_SIZE + 1
#define WINDOW_SIZE 100 // must be < 256 unless packet ID byte count increases
#define TIMEOUT_SECS 60

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
	
	int infd = open(infile_path, O_RDONLY, 0664);
	if (infd < 0) {
		fprintf(stderr, "myclient ~ main(): failed to open file %s\n", infile_path);
		exit(1);
	}

	int outfd;

	if (create_file_directory(outfile_path) < 0) {
		fprintf(stderr, "myclient ~ main(): failed to create outfile directories. Attempting to open file anyway.\n");
		outfd = open(outfile_path, O_RDWR | O_CREAT | O_TRUNC, 0664);
		if (outfd < 0) {
			fprintf(stderr, "myclient ~ main(): failed to open/create outfile %s\n", outfile_path);
			exit(1);
		}
		fprintf(stderr, "myclient ~ main(): outfile creation successful.\n");
	} else {
		outfd = open(outfile_path, O_RDWR | O_CREAT | O_TRUNC, 0664);
		if (outfd < 0) {
			fprintf(stderr, "myclient ~ main(): failed to open/create outfile %s\n", outfile_path);
			exit(1);
		}
	}

	// initialize socket
	struct sockaddr_in serveraddr;
	socklen_t serveraddr_size = sizeof(serveraddr);
	int sockfd = init_socket(&serveraddr, server_ip, server_port, AF_INET, SOCK_DGRAM, IPPROTO_UDP, false);
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

int send_window_packets(int infd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mss, int window_size, uint8_t client_id, uint8_t *last_packet_id_expected) {
	char buf[mss+1];
	memset(buf, 0, sizeof(buf));

	int bytes_read = 1, eof_reached = 0;

	uint8_t packet_id_sent;

	// printf("sending packet window\n");

	for (packet_id_sent = 1; packet_id_sent <= window_size; packet_id_sent++) {
		// printf("sending packet %u\n", packet_id_sent);
		bytes_read = read(infd, buf+HEADER_SIZE, mss-HEADER_SIZE);
		// printf("made it past read\n");
		if (bytes_read < 0) {
			fprintf(stderr, "myclient ~ send_recv_file(): encountered an error reading from infile.\n");
			return -1;
		} else if (bytes_read == 0) {
			buf[HEADER_SIZE] = 0;
			eof_reached = 1;
		}
		
		// assign packet ID
		buf[0] = packet_id_sent;

		// assign client ID
		buf[1] = client_id;

		buf[HEADER_SIZE+mss] = 0;

		if (sendto(sockfd, buf, bytes_read+HEADER_SIZE, 0, sockaddr, sockaddr_size) < 0) {
			fprintf(stderr, "myclient ~ send_recv_file(): client failed to send packet to server.\n");
			return -1;
		}

		if (eof_reached) {
			break; // finished reading file, now just wait to receive rest of packets
		}

		*last_packet_id_expected = packet_id_sent;

		memset(buf, 0, sizeof(buf));
	}

	return bytes_read;
}

int recv_window_packets(int outfd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mss, uint8_t last_packet_id_expected, uint8_t *ooo_packet_ids, off_t *ooo_packet_locations, uint8_t *client_id) {
	char buf[mss+1];
	memset(buf, 0, sizeof(buf));

	uint8_t packet_id_recvd;

	fd_set fds;
	FD_SET(sockfd, &fds);

	struct timeval timeout = { TIMEOUT_SECS, 0 };
	// timeout.tv_sec = TIMEOUT_SECS; // 60s timeout for recvfrom server

	int num_packets_not_recvd = 0; // number of packets still out (data in use in above arrays)
	int bytes_recvd, num_packets_recvd = 0;

	uint8_t expected_packet_id_recv = 1;

	while (num_packets_recvd < last_packet_id_expected) {
		int res;
		timeout.tv_sec = TIMEOUT_SECS;
		if ((res = select(sockfd + 1, &fds, NULL, NULL, &timeout)) > 0) { // check there is data to be read from socket
			if ((bytes_recvd = recvfrom(sockfd, buf, mss, 0, sockaddr, &sockaddr_size)) < 0) {
				fprintf(stderr, "myclient ~ send_recv_file(): an error occured while receiving data from server.\n");
				fprintf(stderr, "%s\n", strerror(errno));
				return -1;
			} else { // recvfrom succeeds
				if (bytes_recvd == HEADER_SIZE) {
					fprintf(stderr, "myclient ~ send_recv_file(): Connection refused by server (server is serving another client right now).\n");
					return -1;
				}
				// get client ID from server response to first packet
				if (*client_id != (uint8_t)buf[1]) {
					if (*client_id == 1) {
						*client_id = (uint8_t)buf[1];
					} else {
						continue; // packet was not intended for me
					}
				}

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
		} else { // after 60s timeout
			// if haven't received more packets than ooo packets, can't detect server
			// otherwise we've timed out waiting for a dropped packet
			if (last_packet_id_expected - num_packets_recvd > num_packets_not_recvd) {
				fprintf(stderr, "Cannot detect server.\n");
			} else {
				fprintf(stderr, "Packet loss detected.\n");
			}
			return -1;
		}

		num_packets_recvd += 1;

		memset(buf, 0, sizeof(buf));
	}

	return 0;
}

// send file from fd to sockfd, also using sockaddr
// return 0 on success, -1 on error
int send_recv_file(int infd, int outfd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mss) {
	
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

	uint8_t ooo_packet_ids[WINDOW_SIZE];
	off_t ooo_packet_locations[WINDOW_SIZE];
	for (int i = 0; i < WINDOW_SIZE; i++) {
		ooo_packet_ids[i] = 0;
		ooo_packet_locations[i] = 0;
	}

	// printf("\n\nooo_packet_ids: %ld, &ooo_packet_ids: %ld, (uint8_t **)&ooo_packet_idx: %ld\n\n", (intptr_t)ooo_packet_ids, &ooo_packet_ids, (uint8_t **)ooo_packet_ids);

	int bytes_read = 1;

	uint8_t client_id = 1;
	uint8_t last_packet_id_expected;

	// test connection first by sending only one packet
	bytes_read = send_window_packets(infd, sockfd, sockaddr, sockaddr_size, mss, 1, client_id, &last_packet_id_expected);
	if (bytes_read < 0) {
		fprintf(stderr, "myclient ~ send_recv_file(): initial packet send failed\n");
		return -1;
	}

	if (recv_window_packets(outfd, sockfd, sockaddr, sockaddr_size, mss, last_packet_id_expected, ooo_packet_ids, ooo_packet_locations, &client_id) < 0) {
		fprintf(stderr, "myclient ~ send_recv_file(): initial packet recv failed\n");
		return -1;
	}

	while (bytes_read > 0) { // continue until no more bytes read from file
		bytes_read = send_window_packets(infd, sockfd, sockaddr, sockaddr_size, mss, WINDOW_SIZE, client_id, &last_packet_id_expected);
		if (bytes_read < 0) {
			fprintf(stderr, "myclient ~ send_recv_file(): send_window_packets() failed\n");
			return -1;
		}

		if (recv_window_packets(outfd, sockfd, sockaddr, sockaddr_size, mss, last_packet_id_expected, ooo_packet_ids, ooo_packet_locations, &client_id) < 0) {
			fprintf(stderr, "myclient ~ send_recv_file(): recv_window_packets() failed\n");
			return -1;
		}
	}

	return 0;
}

int create_file_directory(const char *file_path) {
	// regex for matching last / in filepath
	regex_t regex_;
	if (regcomp(&regex_, "/", 0) < 0) {
		fprintf(stderr, "myclient ~ create_file_directory(): regcomp() failed\n");
		return -1;
	}

	regmatch_t pmatch;
	int res = 0, fs_idx = 0;

	while (res != REG_NOMATCH) {
		if ((res = regexec(&regex_, file_path + fs_idx, 1, &pmatch, 0)) < 0) {
			fprintf(stderr, "myclient ~ create_file_directory(): regexec() failed\n");
			return -1;
		} else if (res != REG_NOMATCH) {
			fs_idx += pmatch.rm_eo;

			char outfile_dir[fs_idx + 1];
			outfile_dir[fs_idx] = 0;
			memcpy(outfile_dir, file_path, fs_idx );

			struct stat st = {0};
			if (stat(outfile_dir, &st) == -1) {
				if (mkdir(outfile_dir, 0700) < 0) {
					fprintf(stderr, "myclient ~ create_file_directory(): failed to make directory %s\n", outfile_dir);
					return -1;
				}
			}
		}
	}
	
	regfree(&regex_);

	return 0;
}
