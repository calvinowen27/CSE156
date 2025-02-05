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
#include <time.h>
#include <sys/errno.h>

#include "myclient.h"
#include "utils.h"
#include "protocol.h"

#define MIN_MSS_SIZE MAX_HEADER_SIZE + 1

struct pkt_ack_info {
	off_t file_idx;
	bool ackd;
	int retransmits;
	bool active;
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
	u_int32_t winsz = strtoull(argv[4], &endptr, 10);											// winsz
	
	// check for valid winsz
	if (winsz < 1 || winsz > 0xffffffff || *endptr != '\0') {
		printf("Invalid winsz (window size) provided. Please provide a positive integer for winsz.\n");
		exit(1);
	}

	if (winsz > 0xffffffff) {
		printf("Invalid winsz (window size) provided. winz cannot be larger than %d.\n", 0xffffffff);
		exit(1);
	}

	char *infile_path = argv[5];															// infile path
	char *outfile_path = argv[6];															// outfile path
	
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
	if (send_file(infd, outfile_path, sockfd, (struct sockaddr *)&serveraddr, &serveraddr_size, mss, winsz) < 0) {
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
int send_file(int infd, const char *outfile_path, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, u_int32_t winsz) {
	u_int32_t client_id, start_pkt_sn = 0, ack_pkt_sn = -1;

	// initiate handshake with WR and outfile path
	if (perform_handshake(sockfd, outfile_path, sockaddr, sockaddr_size, &client_id, winsz) < 0) {
		fprintf(stderr, "myclient ~ send_file(): encountered an error while performing handshake with server.\n");
		return -1;
	}

	// once we're here, we should have client id value in client_id, meaning handshake is complete

	bool need_pkt_resend, reached_eof = false;

	// need to resend pkts if ack sn < last pkt sn sent
	// send pkts from ack sn + 1
	int pkts_sent;

	struct pkt_ack_info pkt_info[winsz];
	reset_pkt_info(pkt_info, winsz);

	bool outfile_path_done = false;
	int outfile_idx = 0;

	// continue while eof hasn't been reached or pkts need to be resent
	while (!reached_eof || need_pkt_resend) {
		need_pkt_resend = false;

		if (outfile_path_done) {
			// send pkt window
			if ((pkts_sent = send_window_pkts(infd, sockfd, sockaddr, sockaddr_size, mss, winsz, client_id, start_pkt_sn, pkt_info)) < 0) {
				fprintf(stderr, "myclient ~ send_file(): encountered an error while sending pkt window.\n");
				return -1;
			}

			reached_eof = pkts_sent == 0; // update reached_eof
		} else {
			// send window for outfile path
			if ((pkts_sent = send_outfile_path(outfile_path, &outfile_idx, sockfd, sockaddr, sockaddr_size, mss, winsz, client_id, start_pkt_sn, pkt_info)) < 0) {
				fprintf(stderr, "myclient ~ send_file(): encountered an error while sending pkt window.\n");
				return -1;
			} else if (pkts_sent == 0) {
				outfile_path_done = true;
				reset_pkt_info(pkt_info, winsz);
			}
		}

		// wait for server response, to get ack sn
		if (recv_server_response(sockfd, sockaddr, sockaddr_size, &ack_pkt_sn) < 0) {
			fprintf(stderr, "myclient ~ send_file(): encourntered an error while trying to receive server response.\n");
			return -1;
		}

		if (reached_eof) break;

		// update pkt info with ack
		for (u_int32_t sn = 0; sn < winsz; sn++) {
			struct pkt_ack_info *pkt = &pkt_info[sn];
			if (pkt->active) {
				if ((ack_pkt_sn > start_pkt_sn && sn >= start_pkt_sn && sn <= ack_pkt_sn) || (ack_pkt_sn < start_pkt_sn && (sn >= start_pkt_sn || sn <= ack_pkt_sn))) {
					pkt->ackd = true;
					pkt->active = false;
				} else if (pkt->retransmits > 3) {
					fprintf(stderr, "myclient ~ send_file(): exceeded 3 retransmits for a single packet.\n");
					exit(1); // TODO: make sure this is the right exit code for failure/too many retransmissions
				} else {
					need_pkt_resend = true;
				}
			}
		}

		start_pkt_sn = ack_pkt_sn + 1;
		if (start_pkt_sn == winsz) start_pkt_sn = 0; // wrap back
	}

	return 0;
}

// initiate handshake with server, which should respond with the client id
// client id value is put in *client_id
// return 0 on success, -1 on error
int perform_handshake(int sockfd, const char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t *client_id, u_int32_t winsz) {
	// construct WR packet
	char handshake_buf[WR_HEADER_SIZE];				// null terminated and opcode both 1 byte
	handshake_buf[0] = OP_WR;										// set WR opcode

	(void)outfile_path;

	if (assign_wr_winsz(handshake_buf, winsz) < 0) {
		fprintf(stderr, "myclient ~ perform_handshake(): encountered error assigning window size to handshake buffer.\n");
		return -1;
	}

	int recv_res = 1;

	do {
		fprintf(stderr, "Initial write request packet sent.\n");

		// send WR to server
		if (sendto(sockfd, handshake_buf, sizeof(handshake_buf), 0, sockaddr, *sockaddr_size) < 0) {
			fprintf(stderr, "myclient ~ perform_handshake(): client failed to send WR pkt to server.\n");
			return -1;
		}

		if (log_pkt(handshake_buf) < 0) {
			fprintf(stderr, "myclient ~ perform_handshake(): encountered error logging pkt info.\n");
			return -1;
		}
	} while ((recv_res = recv_server_response(sockfd, sockaddr, sockaddr_size, client_id)) == 1);

	if (recv_res < 0) {
		fprintf(stderr, "myclient ~ perform_handshake(): failed to recv server handshake response.\n");
		return -1;
	}

	fprintf(stderr, "Connection confirmed by server.\n");

	return 0;
}

// send window of pkts with content from infd
// returns number of pkts sent, -1 on error
int send_window_pkts(int infd, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, u_int32_t winsz, u_int32_t client_id, u_int32_t start_pkt_sn, struct pkt_ack_info *pkt_info) {
	char pkt_buf[mss];
	memset(pkt_buf, 0, sizeof(pkt_buf));

	// reset pkt info array
	for (u_int32_t sn = 0; sn < winsz; sn++) {
		struct pkt_ack_info *pkt = &pkt_info[sn];

		if (pkt->ackd) {
			pkt->retransmits = 0;
			pkt->active = false;
			pkt->ackd = false;
		} else if (pkt->active) {
			pkt->retransmits ++;
		}
	}

	// check start pkt for ack, if no ack, go back to file idx
	struct pkt_ack_info *pkt = &pkt_info[start_pkt_sn];
	if (!pkt->ackd && pkt->active) {
		lseek(infd, pkt->file_idx, SEEK_SET);
	}

	u_int32_t pyld_sz;

	bool eof_reached = false;

	u_int32_t pkt_sn = start_pkt_sn;
	u_int32_t pkts_sent = 0;
	int bytes_read;

	while (pkts_sent < winsz && !eof_reached) { // only send max winsz packets
		pkt = &pkt_info[pkt_sn];
		pkt->file_idx = lseek(infd, 0, SEEK_CUR);

		pkt->active = true;
		pkt->ackd = false;

		// bytes_read is our payload size
		bytes_read = read(infd, pkt_buf + DATA_HEADER_SIZE, ((u_int32_t)mss) - ((u_int32_t)DATA_HEADER_SIZE));
		pyld_sz = (u_int32_t)bytes_read;

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
		assign_pkt_pyld_sz(pkt_buf, pyld_sz);

		if (sendto(sockfd, pkt_buf, DATA_HEADER_SIZE + pyld_sz, 0, sockaddr, *sockaddr_size) < 0) {
			fprintf(stderr, "myclient ~ send_window_pkts(): client failed to send packet to server.\n");
			return -1;
		}

		if (log_pkt(pkt_buf) < 0) {
			fprintf(stderr, "myclient ~ send_window_pkts(): encountered error logging pkt info.\n");
			return -1;
		}

		if (eof_reached) fprintf(stderr, "End of file.\n");

		pkt_sn++;
		if (pkt_sn == winsz) pkt_sn = 0; // wrap back to unused pkt_info

		pkts_sent++;

		memset(pkt_buf, 0, sizeof(pkt_buf));
	}

	if (eof_reached) return 0;

	return pkts_sent;
}

// wait for server response, ack_pkt_sn is output
// return 0 on success, 1 on resend, -1 on error
int recv_server_response(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t *ack_pkt_sn) {
	char pkt_buf[MAX_SRVR_RES_SIZE];
	memset(pkt_buf, 0, sizeof(pkt_buf));

	// configure fds and timeout for select() call
	fd_set fds;
	FD_SET(sockfd, &fds);

	struct timeval timeout = { TIMEOUT_SECS, 0 };

	int bytes_recvd;

	if (select(sockfd + 1, &fds, NULL, NULL, &timeout) > 0) { // check there is data to be read from socket
		if ((bytes_recvd = recvfrom(sockfd, pkt_buf, sizeof(pkt_buf), 0, sockaddr, sockaddr_size)) >= 0) {
			if (log_pkt(pkt_buf) < 0) {
				fprintf(stderr, "myclient ~ send_window_pkts(): encountered error logging pkt info.\n");
				return -1;
			}
			
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
		fprintf(stderr, "myclient ~ recv_server_response(): No server response. Retrying.\n");
		return 1;
	}

	return 0;
}

// send window of pkts with outfile path as content
// returns number of pkts sent, -1 on error
int send_outfile_path(const char *outfile_path, int *path_idx, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, u_int32_t winsz, u_int32_t client_id, u_int32_t start_pkt_sn, struct pkt_ack_info *pkt_info) {
	char pkt_buf[mss];
	memset(pkt_buf, 0, sizeof(pkt_buf));

	// reset pkt info array
	for (u_int32_t sn = 0; sn < winsz; sn++) {
		struct pkt_ack_info *pkt = &pkt_info[sn];

		if (pkt->ackd) {
			pkt->retransmits = 0;
			pkt->active = false;
			pkt->ackd = false;
		} else if (pkt->active) {
			pkt->retransmits ++;
		}
	}

	// check start pkt for ack, if no ack, go back to file idx
	struct pkt_ack_info *pkt = &pkt_info[start_pkt_sn];
	if (!pkt->ackd && pkt->active) {
		*path_idx = pkt->file_idx;	// use file idx for path idx in this function
	}

	u_int32_t pyld_sz;

	bool eop_reached = false;

	u_int32_t pkt_sn = start_pkt_sn;
	u_int32_t pkts_sent = 0;
	int bytes_read;

	u_int32_t pkt_space = ((u_int32_t)mss) - ((u_int32_t)DATA_HEADER_SIZE);

	while (pkts_sent < winsz && !eop_reached) { // only send max winsz packets
		pkt = &pkt_info[pkt_sn];
		pkt->file_idx = *path_idx;

		pkt->active = true;
		pkt->ackd = false;

		// bytes_read is our payload size
		// bytes_read = read(infd, pkt_buf + DATA_HEADER_SIZE, ((u_int32_t)mss) - ((u_int32_t)DATA_HEADER_SIZE));
		bytes_read = strlen(outfile_path + pkt->file_idx) > pkt_space ? pkt_space : strlen(outfile_path + pkt->file_idx);
		
		memcpy(pkt_buf + DATA_HEADER_SIZE, outfile_path + pkt->file_idx, bytes_read);
		
		pyld_sz = (u_int32_t)bytes_read;

		if (bytes_read < 0) {
			fprintf(stderr, "myclient ~ send_outfile_path(): encountered an error determining path length left.\n");
			return -1;
		} else if (pyld_sz < pkt_space) {
			pkt_buf[bytes_read] = 0; // null terminate
			pyld_sz ++;
			eop_reached = true;
		}
		
		// assign opcode
		assign_pkt_opcode(pkt_buf, OP_DATA);	// assign WR for logging, change to DATA later

		// assign client ID
		assign_pkt_client_id(pkt_buf, client_id);

		// assign packet sequence num
		assign_pkt_sn(pkt_buf, pkt_sn);

		// assign payload size
		assign_pkt_pyld_sz(pkt_buf, pyld_sz);

		if (sendto(sockfd, pkt_buf, DATA_HEADER_SIZE + pyld_sz, 0, sockaddr, *sockaddr_size) < 0) {
			fprintf(stderr, "myclient ~ send_outfile_path(): client failed to send packet to server.\n");
			return -1;
		}

		// assign opcode
		assign_pkt_opcode(pkt_buf, OP_WR);

		if (log_pkt(pkt_buf) < 0) {
			fprintf(stderr, "myclient ~ send_outfile_path(): encountered error logging pkt info.\n");
			return -1;
		}

		if (eop_reached) fprintf(stderr, "End of outfile path.\n");

		pkt_sn++;
		if (pkt_sn == winsz) pkt_sn = 0; // wrap back to unused pkt_info

		pkts_sent++;

		*path_idx += bytes_read;

		memset(pkt_buf, 0, sizeof(pkt_buf));
	}

	if (eop_reached) return 0;

	return pkts_sent;
}

// prints log message of pkt
// returns 0 on success, -1 on error
int log_pkt(char *pkt_buf) {
	time_t t = time(NULL);
	struct tm *tm = gmtime(&t);

	u_int32_t opcode = get_pkt_opcode(pkt_buf);
	if (opcode == 0) {
		fprintf(stderr, "myclient ~ log_pkt(): encountered error getting opcode from pkt.\n");
		return -1;
	}

	if (opcode < OP_WR || opcode > OP_DATA) {
		fprintf(stderr, "myclient ~ log_pkt(): opcode %u is not supported by server.\n", opcode);
		return -1;
	}

	char *opstring = opcode == OP_ACK ? "ACK" : (opcode == OP_WR ? "CTRL" : "DATA");

	u_int32_t sn = opcode == OP_WR ? 0 : (opcode == OP_ACK ? get_ack_sn(pkt_buf) : get_data_sn(pkt_buf));
	if (sn == 0 && errno == 1) {
		fprintf(stderr, "myclient ~ log_pkt(): encountered an error getting pkt sn from pkt.\n");
		return -1;
	}

	printf("%d-%02d-%02dT%02d:%02d:%02dZ, %s, %u\n", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, opstring, sn);

	return 0;
}

void reset_pkt_info(struct pkt_ack_info *pkt_info, u_int32_t winsz) {
	for (u_int32_t sn = 0; sn < winsz; sn++) {
		pkt_info[sn].active = false;
		pkt_info[sn].retransmits = 0;
		pkt_info[sn].ackd = false;
		pkt_info[sn].file_idx = 0;
	}
}
