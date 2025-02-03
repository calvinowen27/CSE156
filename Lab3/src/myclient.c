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

struct pkt_ack_info {
	uint32_t pkt_sn;
	off_t file_idx;
	bool ackd;
	int retransmits;
};

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

	char *endptr;
	uint32_t winsz = strtoull(argv[4], &endptr, 10);											// winsz
	
	// check for valid winsz
	if (winsz < 1 || winsz > 0xffffffff || *endptr == '\0') {
		printf("Invalid winsz (window size) provided. Please provide a positive integer for winsz.\n");
		exit(1);
	}

	if (winsz > 0xffffffff) {
		printf("Invalid winsz (window size) provided. winz cannot be larger than %d.\n", 0xffffffff);
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

	// initialize socket
	struct sockaddr_in serveraddr;
	socklen_t serveraddr_size = sizeof(serveraddr);
	int sockfd = init_socket(&serveraddr, server_ip, server_port, AF_INET, SOCK_DGRAM, IPPROTO_UDP, false);
	if (sockfd < 0) {
		fprintf(stderr, "myclient ~ main(): failed to initialize socket.\n");
		exit(1);
	}

	// send in file to server
	if (send_file(infd, outfile_path, sockfd, (struct sockaddr *)&serveraddr, serveraddr_size, mss, winsz) < 0) {
		fprintf(stderr, "myclient ~ main(): failed to send or receive file %s to server.\n", infile_path);
		exit(1);
	}

	// TODO: make sure all exit codes match spec

	close(sockfd);
	close(infd);

	return 0;
}

// send file from fd to sockfd, also using sockaddr
// return 0 on success, -1 on error
int send_file(int infd, const char *outfile_path, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mss, uint32_t winsz) {
	uint32_t client_id, start_pkt_sn = 0, ack_pkt_sn = -1;

	// initiate handshake with WR and outfile path
	if (perform_handshake(sockfd, outfile_path, sockaddr, sockaddr_size, &client_id) < 0) {
		fprintf(stderr, "myclient ~ send_file(): encountered an error while performing handshake with server.\n");
		return -1;
	}

	// once we're here, we should have client id value in client_id, meaning handshake is complete

	bool reached_eof = false;
	bool need_pkt_resend = false;

	// need to resend pkts if ack sn < last pkt sn sent
	// send pkts from ack sn + 1
	uint32_t pkts_sent;

	struct pkt_ack_info pkt_info[winsz];
	for (uint32_t i = 0; i < winsz; i++) {
		pkt_info[i].ackd = true;
	}

	// continue while eof hasn't been reached or pkts need to be resent
	while (!reached_eof || need_pkt_resend) {
		// send pkt window
		if ((pkts_sent = send_window_pkts(infd, sockfd, sockaddr, sockaddr_size, mss, winsz, client_id, start_pkt_sn, pkt_info)) < 0) {
			fprintf(stderr, "myclient ~ send_file(): encountered an error while sending pkt window.\n");
			return -1;
		}

		reached_eof = pkts_sent < winsz; // update reached_eof

		// wait for server response, to get ack sn
		if (recv_server_response(sockfd, sockaddr, sockaddr_size, &ack_pkt_sn) < 0) {
			fprintf(stderr, "myclient ~ send_file(): encourntered an error while trying to receive server response.\n");
			return -1;
		}
	
		// update pkt info with ack
		for (uint32_t i = 0; i < winsz; i++) {
			struct pkt_ack_info pkt = pkt_info[i];
			if (pkt.pkt_sn >= start_pkt_sn && pkt.pkt_sn <= ack_pkt_sn) {
				pkt.ackd = true;
			} else {
				if (pkt.retransmits >= 3) {
					fprintf(stderr, "myclient ~ send_file(): exceeded 3 retransmits for a single packet.\n");
					exit(1); // TODO: make sure this is the right exit code for failure/too many retransmissions
				}
			}
		}

		need_pkt_resend = ack_pkt_sn != start_pkt_sn + pkts_sent; // != to work for wraparound, > case isn't possible(?)

		start_pkt_sn = ack_pkt_sn + 1;
	}

	return 0;
}

// initiate handshake with server, which should respond with the client id
// client id value is put in *client_id
// return 0 on success, -1 on error
int perform_handshake(int sockfd, const char *outfile_path, struct sockaddr *sockaddr, socklen_t sockaddr_size, uint32_t *client_id) {
	// construct WR packet
	char handshake_buf[1 + strlen(outfile_path) + 1];				// null terminated and opcode both 1 byte
	handshake_buf[0] = OP_WR;										// set WR opcode
	handshake_buf[sizeof(handshake_buf) - 1] = 0;					// null terminate
	memcpy(handshake_buf + 1, outfile_path, strlen(outfile_path));	// copy outfile_path to pkt

	// send WR to server
	if (sendto(sockfd, handshake_buf, sizeof(handshake_buf), 0, sockaddr, sockaddr_size) < 0) {
		fprintf(stderr, "myclient ~ perform_handshake(): client failed to send WR pkt to server.\n");
		return -1;
	}

	// client id is in sn spot of first ack, so pass &client_id to ack_pkt_sn*
	if (recv_server_response(sockfd, sockaddr, sockaddr_size, client_id) < 0) {
		fprintf(stderr, "myclient ~ perform_handshake(): failed to recv server handshake response.\n");
		return -1;
	}

	return 0;
}

// send window of pkts with content from infd
// returns number of pkts sent, -1 on error
int send_window_pkts(int infd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mss, int winsz, uint32_t client_id, uint32_t start_pkt_sn, struct pkt_ack_info *pkt_info) {
	char pkt_buf[mss];
	memset(pkt_buf, 0, sizeof(pkt_buf));

	uint32_t bytes_read;

	bool eof_reached = false;

	uint32_t pkt_sn = start_pkt_sn;
	int pkts_sent = 0;

	int free_info_idx;
	bool pkt_found = false;
	while (pkts_sent < winsz) { // only send max winsz packets
		// update info
		free_info_idx = -1;
		for (int i = 0; i < winsz; i++) {
			struct pkt_ack_info pkt = pkt_info[i];
			if (pkt.pkt_sn == pkt_sn) {
				// seek file idx of pkt
				lseek(infd, pkt.file_idx, SEEK_SET);
				pkt_found = true;
				pkt.retransmits++;
				break;
			} else if (free_info_idx == -1 && pkt.ackd) { // find first ackd (not needed) pkt
				free_info_idx = i;
			}
		}

		// if pkt sn not found, add at free_info_idx
		if (!pkt_found && free_info_idx != -1) {
			struct pkt_ack_info pkt = pkt_info[free_info_idx];
			pkt.pkt_sn = pkt_sn;
			pkt.ackd = false;
			pkt.file_idx = lseek(infd, 0, SEEK_CUR);
			pkt.retransmits = 0;
		}

		// bytes_read is our payload size
		bytes_read = (uint32_t)read(infd, pkt_buf + DATA_HEADER_SIZE, mss - DATA_HEADER_SIZE);
		
		if (bytes_read < 0) {
			fprintf(stderr, "myclient ~ send_window_pkts(): encountered an error reading from infile.\n");
			return -1;
		} else if (bytes_read == 0) {
			eof_reached = true;
		}
		
		// assign opcode
		assign_pkt_opcode(pkt_buf, OP_DATA);

		// assign client ID
		assign_pkt_client_id(pkt_buf, client_id);

		// assign packet sequence num
		assign_pkt_sn(pkt_buf, pkt_sn);

		// assign payload size
		assign_pkt_pyld_sz(pkt_buf, bytes_read);

		if (sendto(sockfd, pkt_buf, DATA_HEADER_SIZE + bytes_read, 0, sockaddr, sockaddr_size) < 0) {
			fprintf(stderr, "myclient ~ send_window_pkts(): client failed to send packet to server.\n");
			return -1;
		}

		pkt_sn++; // wraparound is fine

		pkts_sent++;

		memset(pkt_buf, 0, sizeof(pkt_buf));

		if (eof_reached) {
			break; // finished reading file, now just wait to receive rest of packets
		}
	}

	return pkts_sent;
}

// wait for server response, ack_pkt_sn is output
// return 0 on success, -1 on error
int recv_server_response(int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, uint32_t *ack_pkt_sn) {
	char pkt_buf[MAX_SRVR_RES_SIZE];
	memset(pkt_buf, 0, sizeof(pkt_buf));

	// configure fds and timeout for select() call
	fd_set fds;
	FD_SET(sockfd, &fds);

	struct timeval timeout = { TIMEOUT_SECS, 0 };

	int bytes_recvd;

	if (select(sockfd + 1, &fds, NULL, NULL, &timeout) > 0) { // check there is data to be read from socket
		if ((bytes_recvd = recvfrom(sockfd, pkt_buf, sizeof(pkt_buf), 0, sockaddr, &sockaddr_size)) >= 0) {
			// recvfrom success
			int opcode = get_pkt_opcode(pkt_buf);
			switch (opcode) {
				case OP_ACK:
					*ack_pkt_sn = get_ack_sn(pkt_buf);	// assign pkt sn to ack_pkt_sn
					break;
				case OP_ERROR:
					// TODO: idk how this is supposed to be handled tbh so make sure it's right
					fprintf(stderr, "myclient ~ recv_server_response(): received error from server, exiting.\n");
					exit(1);	
					break;
				default:
					// TODO: make sure this is implemented correctly too
					fprintf(stderr, "myclient ~ recv_server_response(): unrecognized opcode received from server: %d.\n", opcode);
					return -1;
					break;
			};			
		} else { // recvfrom failed
			fprintf(stderr, "myclient ~ recv_server_response(): an error occured while receiving data from server.\n");
			fprintf(stderr, "%s\n", strerror(errno));
			return -1;
		}
	} else { // after timeout
		// TODO: retransmit pkt window since we didn't hear back from the server, keep track of retransmits per pkt
		//		also make sure to update the error message V V V
		fprintf(stderr, "myclient ~ recv_server_response(): no server response. Here we should resend pkt window (not implemented).\n");
		return -1;
	}

	return 0;
}
