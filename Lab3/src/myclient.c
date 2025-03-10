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

#include "myclient.h"
#include "utils.h"
#include "protocol.h"

#define MIN_MSS_SIZE MAX_HEADER_SIZE + 1

struct c_pkt_info {
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
		fprintf(stderr, "Required minimum MSS is %d\n", MIN_MSS_SIZE);
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

	if (strlen(outfile_path) > (size_t)(mss - MAX_HEADER_SIZE) - 1) {
		printf("MSS argument is too small for desired output file path. MSS value specified is %d bytes and header length is %d bytes. Please specify an outfile path that is less than or equal to %d - %d - 1 = %d bytes long, or provide a larger MSS\n", mss, WR_HEADER_SIZE, mss, WR_HEADER_SIZE, mss - (WR_HEADER_SIZE + 1));
		exit(1);
	}

	if (strlen(outfile_path) > 4096) {
		printf("outfile path name cannot exceed 4096 characters.\n");
		exit(1);
	}
	
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

	struct timeval server_timeout = { TIMEOUT_SECS, 0 };
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &server_timeout, sizeof(server_timeout)) < 0) {
		fprintf(stderr, "myclient ~ main(): encountered error trying to set socket timeout: %s\n", strerror(errno));
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

	bool handshake_confirmed = false;
	int handshake_retransmits = 0;

	start_pkt_sn = (client_id + 2) % (2 * winsz);

	// once we're here, we should have client id value in client_id, meaning handshake is complete

	bool need_pkt_resend, reached_eof = false;

	// need to resend pkts if ack sn < last pkt sn sent
	// send pkts from ack sn + 1
	int pkts_sent;

	struct c_pkt_info pkt_info[2 * winsz];
	for (u_int32_t sn = 0; sn < 2 * winsz; sn++) {
		pkt_info[sn].active = false;
		pkt_info[sn].retransmits = 0;
		pkt_info[sn].ackd = false;
		pkt_info[sn].file_idx = 0;
	}

	int recv_res;
	u_int32_t last_sent_sn;

	// continue while eof hasn't been reached or pkts need to be resent
	while (!reached_eof || need_pkt_resend) {
		need_pkt_resend = false;

		do {
			// send pkt window
			if ((pkts_sent = send_window_pkts(infd, sockfd, sockaddr, sockaddr_size, mss, winsz, client_id, start_pkt_sn, pkt_info, &last_sent_sn)) < 0) {
				fprintf(stderr, "myclient ~ send_file(): encountered an error while sending pkt window.\n");
				return -1;
			}

			reached_eof = pkts_sent == 0; // update reached_eof

			// wait for server response, to get ack sn
			recv_res = recv_server_response(sockfd, sockaddr, sockaddr_size, &ack_pkt_sn, start_pkt_sn, winsz);

			if (!handshake_confirmed) {
				if (ack_pkt_sn == client_id) {
					char handshake_buf[5];
					handshake_buf[0] = OP_WR; // only for logging
					if (assign_ack_sn(handshake_buf, client_id) < 0) {
						fprintf(stderr, "myclient ~ perform_handshake(): encountered error assigning client ID to handshake buffer.\n");
						return -1;
					}

					if (log_pkt(handshake_buf, client_id, winsz) < 0) {
						fprintf(stderr, "myclient ~ perform_handshake(): encountered error logging pkt info.\n");
						return -1;
					}

					handshake_buf[0] = OP_ACK;

					if (sendto(sockfd, handshake_buf, sizeof(handshake_buf), 0, sockaddr, *sockaddr_size) < 0) {
						fprintf(stderr, "myclient ~ perform_handshake(): client failed to send final ACK to server.\n");
						return -1;
					}

					fprintf(stderr, "Connection reconfirmed by server.\n");

					handshake_retransmits ++;
					if (handshake_retransmits > 3) {
						fprintf(stderr, "Reached max re-transmission limit\n");
						exit(4);
					}

					continue;
				} else {
					handshake_confirmed = true;
				}
			}
		} while (recv_res == 1);

		if (recv_res < 0) {
			fprintf(stderr, "myclient ~ send_file(): encourntered an error while trying to receive server response.\n");
			return -1;
		}

		if (reached_eof) {
			while (recv_res == 1) {
				recv_res = recv_server_response(sockfd, sockaddr, sockaddr_size, &ack_pkt_sn, start_pkt_sn, winsz);
			}

			if (recv_res < 0) {
				fprintf(stderr, "myclient ~ send_file(): encourntered an error while trying to receive server response.\n");
			} else if (ack_pkt_sn == last_sent_sn) {
				char ack_buf[5];
				ack_buf[0] = OP_ACK;
				if (assign_ack_sn(ack_buf, client_id) < 0) {
					fprintf(stderr, "myclient ~ send_file(): encountered error assigning ack opcode to terminating pkt.\n");
					return -1;
				}

				if (sendto(sockfd, ack_buf, 5, 0, sockaddr, *sockaddr_size) < 0) {
					fprintf(stderr, "myclient ~ send_file(): client failed to send packet to server.\n");
					return -1;
				}

				if (log_pkt(ack_buf, client_id, winsz) < 0) {
					fprintf(stderr, "myclient ~ send_file(): encountered error logging pkt info.\n");
					return -1;
				}

				break;
			}
		}

		// update pkt info with ack
		u_int32_t sn = start_pkt_sn;
		struct c_pkt_info *pkt;
		while (sn != (ack_pkt_sn + 1) % (2 * winsz)) {
			pkt = &pkt_info[sn];

			if (pkt->active) {
				pkt->ackd = true;
				pkt->active = false;
			}

			sn = (sn + 1) % (winsz * 2);
		}

		need_pkt_resend = ack_pkt_sn != (start_pkt_sn + winsz) % (winsz * 2);

		start_pkt_sn = (ack_pkt_sn + 1) % (winsz * 2);
	}

	return 0;
}

// TODO: don't log handshake ACK?

// initiate handshake with server, which should respond with the client id
// client id value is put in *client_id
// return 0 on success, -1 on error
int perform_handshake(int sockfd, const char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t *client_id, u_int32_t winsz) {
	// construct WR packet
	char handshake_buf[WR_HEADER_SIZE + strlen(outfile_path) + 1];				// null terminated and opcode both 1 byte
	handshake_buf[0] = OP_WR;										// set WR opcode

	if (assign_wr_winsz(handshake_buf, winsz) < 0) {
		fprintf(stderr, "myclient ~ perform_handshake(): encountered error assigning window size to handshake buffer.\n");
		return -1;
	}

	memcpy(handshake_buf + WR_HEADER_SIZE, outfile_path, strlen(outfile_path));	// copy outfile_path to pkt
	handshake_buf[sizeof(handshake_buf) - 1] = 0; // null terminate

	int recv_res = 1;
	int retransmits = -1;

	do {
		fprintf(stderr, "Initial write request packet sent.\n");

		retransmits ++;

		if (retransmits > 3) {
			fprintf(stderr, "Reached max re-transmission limit\n");
			exit(4);
		}

		// send WR to server
		if (sendto(sockfd, handshake_buf, sizeof(handshake_buf), 0, sockaddr, *sockaddr_size) < 0) {
			fprintf(stderr, "myclient ~ perform_handshake(): client failed to send WR pkt to server.\n");
			return -1;
		}

		if (log_pkt(handshake_buf, 0, winsz) < 0) {
			fprintf(stderr, "myclient ~ perform_handshake(): encountered error logging pkt info.\n");
			return -1;
		}
	} while ((recv_res = recv_server_response(sockfd, sockaddr, sockaddr_size, client_id, 0, winsz)) == 1);

	if (recv_res < 0) {
		fprintf(stderr, "myclient ~ perform_handshake(): failed to recv server handshake response.\n");
		return -1;
	}

	handshake_buf[0] = OP_WR; // only for logging
	if (assign_ack_sn(handshake_buf, *client_id) < 0) {
		fprintf(stderr, "myclient ~ perform_handshake(): encountered error assigning client ID to handshake buffer.\n");
		return -1;
	}

	if (log_pkt(handshake_buf, *client_id, winsz) < 0) {
		fprintf(stderr, "myclient ~ perform_handshake(): encountered error logging pkt info.\n");
		return -1;
	}

	handshake_buf[0] = OP_ACK;

	if (sendto(sockfd, handshake_buf, sizeof(handshake_buf), 0, sockaddr, *sockaddr_size) < 0) {
		fprintf(stderr, "myclient ~ perform_handshake(): client failed to send final ACK to server.\n");
		return -1;
	}

	fprintf(stderr, "Connection confirmed by server: %u.\n", *client_id);

	return 0;
}

// send window of pkts with content from infd
// returns number of pkts sent, -1 on error
int send_window_pkts(int infd, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, u_int32_t winsz, u_int32_t client_id, u_int32_t start_pkt_sn, struct c_pkt_info *pkt_info, u_int32_t *last_sent_sn) {
	char pkt_buf[mss];
	memset(pkt_buf, 0, sizeof(pkt_buf));

	// reset pkt info array
	for (u_int32_t sn = 0; sn < 2 * winsz; sn++) {
		struct c_pkt_info *pkt = &pkt_info[sn];

		if (pkt->ackd) {
			pkt->retransmits = 0;
			pkt->active = false;
			pkt->ackd = false;
		} else if (pkt->active) {
			pkt->retransmits ++;
			if (pkt->retransmits > 3) {
				fprintf(stderr, "Reached max re-transmission limit\n");
				exit(4);
			}
		}
	}

	// check start pkt for ack, if no ack, go back to file idx
	struct c_pkt_info *pkt = &pkt_info[start_pkt_sn];
	if (!pkt->ackd && pkt->active) {
		lseek(infd, pkt->file_idx, SEEK_SET);
	}

	u_int32_t pyld_sz;

	bool eof_reached = false;

	u_int32_t pkt_sn = start_pkt_sn;
	int pkts_sent = 0;
	int bytes_read;

	while ((u_int32_t)pkts_sent < winsz && !eof_reached) { // only send max winsz packets
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

		(*last_sent_sn) = pkt_sn;

		if (log_pkt(pkt_buf, start_pkt_sn, winsz) < 0) {
			fprintf(stderr, "myclient ~ send_window_pkts(): encountered error logging pkt info.\n");
			return -1;
		}

		if (eof_reached) fprintf(stderr, "End of file.\n");

		pkt_sn = (pkt_sn + 1) % (2 * winsz);

		pkts_sent ++;

		memset(pkt_buf, 0, sizeof(pkt_buf));
	}

	if (eof_reached) return 0;

	return pkts_sent;
}

// wait for server response, ack_pkt_sn is output
// return 0 on success, 1 on resend, -1 on error
int recv_server_response(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t *ack_pkt_sn, u_int32_t start_sn, u_int32_t winsz) {
	char pkt_buf[MAX_SRVR_RES_SIZE];
	memset(pkt_buf, 0, sizeof(pkt_buf));

	// configure fds and timeout for select() call
	fd_set fds;
	FD_SET(sockfd, &fds);

	struct timeval timeout = { LOSS_TIMEOUT_SECS, 0 };

	int bytes_recvd;

	if (select(sockfd + 1, &fds, NULL, NULL, &timeout) > 0) { // check there is data to be read from socket
		if ((bytes_recvd = recvfrom(sockfd, pkt_buf, sizeof(pkt_buf), 0, sockaddr, sockaddr_size)) >= 0) {			
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

			if (log_pkt(pkt_buf, start_sn, winsz) < 0) {
				fprintf(stderr, "myclient ~ send_window_pkts(): encountered error logging pkt info.\n");
				return -1;
			}
		} else { // recvfrom failed
			fprintf(stderr, "Cannot detect server.\n");
			exit(5);
		}
	} else { // after timeout
		// TODO: retransmit pkt window since we didn't hear back from the server, keep track of retransmits per pkt
		//		also make sure to update the error message V V V
		fprintf(stderr, "Packet Loss Detected\n");
		return 1;
	}

	return 0;
}

// prints log message of pkt
// returns 0 on success, -1 on error
int log_pkt(char *pkt_buf, u_int32_t start_sn, u_int32_t winsz) {
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

	u_int32_t sn = opcode == OP_WR ? get_wr_sn(pkt_buf) : (opcode == OP_ACK ? get_ack_sn(pkt_buf) : get_data_sn(pkt_buf));
	if (sn == 0 && errno == 1) {
		fprintf(stderr, "myclient ~ log_pkt(): encountered an error getting pkt sn from pkt.\n");
		return -1;
	}

	printf("%d-%02d-%02dT%02d:%02d:%02dZ, %s, %u, %u, %u, %u\n", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, opstring, sn, start_sn, (sn + 1) % (2 * winsz), (start_sn + winsz) % (2 * winsz));
	// fflush(stdout);

	return 0;
}
