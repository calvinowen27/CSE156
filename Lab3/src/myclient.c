#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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
#include "protocol.h"

#define MIN_MSS_SIZE MAX_HEADER_SIZE + 1

int main(int argc, char **argv) {
	// handle command line args
	if (argc != 7) {
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
	// check for valid mss size
	if (mss < MIN_MSS_SIZE) {
		printf("Invalid mss provided. Please provide a positive integer greater than %d.\n", MIN_MSS_SIZE);
		exit(1);
	}

	int winsz = atoi(argv[4]);																// winsz
	// check for valid winsz
	if (winsz < 1) {
		printf("Invalid winsz (window size) provided. Please prove a positive integer for winsz.\n");
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
		fprintf(stderr, "myclient ~ main(): failed to create outfile directories. Attempting to open outfile anyway.\n");
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
	if (send_recv_file(infd, outfd, sockfd, (struct sockaddr *)&serveraddr, serveraddr_size, mss, winsz) < 0) {
		fprintf(stderr, "myclient ~ main(): failed to send or receive file %s to server.\n", infile_path);
		exit(1);
	}

	close(sockfd);
	close(infd);
	close(outfd);

	return 0;
}

int send_window_packets(int infd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mss, int winsz, uint32_t client_id, uint32_t start_pkt_sn) {
	char pkt_buf[mss];
	memset(pkt_buf, 0, sizeof(pkt_buf));

	uint32_t bytes_read;

	bool eof_reached = false;

	uint32_t pkt_sn_sent = start_pkt_sn;
	int pkts_sent = 0;

	while (pkts_sent < winsz) { // only send max winsz packets
		// bytes_read is our payload size
		bytes_read = (uint32_t)read(infd, pkt_buf + DATA_HEADER_SIZE, mss - DATA_HEADER_SIZE);
		
		if (bytes_read < 0) {
			fprintf(stderr, "myclient ~ send_recv_file(): encountered an error reading from infile.\n");
			return -1;
		} else if (bytes_read == 0) {
			eof_reached = true;
		}
		
		// assign opcode
		assign_pkt_opcode(pkt_buf, OP_DATA);

		// assign client ID
		assign_pkt_client_id(pkt_buf, client_id);

		// assign packet sequence num
		assign_pkt_sn(pkt_buf, pkt_sn_sent);

		// assign payload size
		assign_pkt_pyld_sz(pkt_buf, bytes_read);

		if (sendto(sockfd, pkt_buf, DATA_HEADER_SIZE + bytes_read, 0, sockaddr, sockaddr_size) < 0) {
			fprintf(stderr, "myclient ~ send_recv_file(): client failed to send packet to server.\n");
			return -1;
		}

		pkt_sn_sent++;
		if (pkt_sn_sent == 0) pkt_sn_sent = 1; // account for overflow (wrap back to 1, 0 not allowed)

		pkts_sent++;

		memset(pkt_buf, 0, sizeof(pkt_buf));

		if (eof_reached) {
			break; // finished reading file, now just wait to receive rest of packets
		}
	}

	return pkts_sent;
}

int recv_server_response(int outfd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, uint32_t *ack_pkt_sn) {
	char pkt_buf[MAX_SRVR_RES_SIZE];
	memset(pkt_buf, 0, sizeof(pkt_buf));

	// configure fds and timeout for select() call
	fd_set fds;
	FD_SET(sockfd, &fds);

	struct timeval timeout = { TIMEOUT_SECS, 0 };

	int bytes_recvd;

	uint8_t expected_packet_id_recv = 1;

	if (select(sockfd + 1, &fds, NULL, NULL, &timeout) > 0) { // check there is data to be read from socket
		if ((bytes_recvd = recvfrom(sockfd, pkt_buf, sizeof(pkt_buf), 0, sockaddr, &sockaddr_size)) >= 0) {
			// recvfrom success
			int opcode = get_pkt_opcode(pkt_buf);
			if (opcode == OP_ACK) {
				*ack_pkt_sn = get_pkt_sn(pkt_buf);	// assign pkt sn to ack_pkt_sn
			} else if (opcode == OP_ERROR) {
				// TODO: idk how this is supposed to be handled tbh so make sure it's right
				fprintf(stderr, "myclient ~ recv_server_response(): received error from server, exiting.\n");
				exit(1);
			} else {
				// TODO: make sure this is implemented correctly too
				fprintf(stderr, "myclient ~ recv_server_response(): unrecognized opcode received from server: %d.\n", opcode);
				return -1;
			}
			
		} else { // recvfrom failed
			fprintf(stderr, "myclient ~ recv_server_response(): an error occured while receiving data from server.\n");
			fprintf(stderr, "%s\n", strerror(errno));
			return -1;
		}
	} else { // after timeout
		// TODO: retransmit pkt window since we didn't hear back from the server
		//		also make sure to update the error message V V V
		fprintf(stderr, "myclient ~ recv_server_response(): no server response. Here we should resend pkt window (not implemented).\n");
		return -1;
	}

	return 0;
}

// send file from fd to sockfd, also using sockaddr
// return 0 on success, -1 on error
int send_recv_file(int infd, int outfd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mss, int winsz) {
	
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

	uint8_t ooo_packet_ids[winsz];
	off_t ooo_packet_locations[winsz];
	for (int i = 0; i < winsz; i++) {
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
		bytes_read = send_window_packets(infd, sockfd, sockaddr, sockaddr_size, mss, winsz, client_id, &last_packet_id_expected);
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

// create all directories in file path (if they don't exist)
// return 0 on success, -1 on error
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
