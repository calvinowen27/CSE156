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
	
	struct client *client = init_client(infile_path, outfile_path, server_ip, server_port, mss, winsz);
	if (client == NULL) {
		fprintf(stderr, "myclient ~ main(): encountered error initialize client.\n");
		exit(1); // TODO: different for multiple threads? etc.
	}

	// send in file to server
	if (send_file(client) < 0) {
		fprintf(stderr, "myclient ~ main(): failed to send or receive file %s to server.\n", infile_path);
		exit(1);
	}

	// TODO: make sure all exit codes match spec

	free_client(&client);

	return 0;
}

// initialize client with relevant information, perform handshake with server
// return pointer to client struct on success, NULL on failure
struct client *init_client(const char *infile_path, const char *outfile_path, const char *server_ip, int server_port, int mss, u_int32_t winsz) {
	// check for NULL args
	if (infile_path == NULL) {
		fprintf(stderr, "myclient ~ init_client(): cannot initialize client with NULL infile_path ptr.\n");
		return NULL;
	}

	if (outfile_path == NULL) {
		fprintf(stderr, "myclient ~ init_client(): cannot initialize client with NULL outfile_path ptr.\n");
		return NULL;
	}

	if (server_ip == NULL) {
		fprintf(stderr, "myclient ~ init_client(): cannot initialize client with NULL server_ip ptr.\n");
		return NULL;
	}
	
	// allocate client memory
	struct client *client = malloc(sizeof(struct client));

	if (client == NULL) {
		fprintf(stderr, "myclient ~ init_client(): failed to allocate memory to client.\n");
		return NULL;
	}

	// open infile
	client->infd = open(infile_path, O_RDONLY, 0664);
	if (client->infd < 0) {
		fprintf(stderr, "myclient ~ init_client(): failed to open file %s\n", infile_path);
		return NULL;
	}

	// save outfile path
	client->outfile_path = outfile_path;

	// init socket info
	client->serveraddr_size = sizeof(client->serveraddr);
	
	client->sockfd = init_socket((struct sockaddr_in *)&client->serveraddr, server_ip, server_port, AF_INET, SOCK_DGRAM, IPPROTO_UDP, false);
	if (client->sockfd < 0) {
		fprintf(stderr, "myclient ~ init_client(): failed to initialize socket.\n");
		return NULL;
	}

	// set socket timeout
	struct timeval server_timeout = { TIMEOUT_SECS, 0 };
	if (setsockopt(client->sockfd, SOL_SOCKET, SO_RCVTIMEO, &server_timeout, sizeof(server_timeout)) < 0) {
		fprintf(stderr, "myclient ~ init_client(): encountered error trying to set socket timeout: %s\n", strerror(errno));
		return NULL;
	}

	client->mss = mss;
	client->winsz = winsz;
	client->pkt_count = 2 * winsz;

	// starts at 0 (invalid), set when handshake takes place
	client->id = 0;

	// init pkt info
	client->pkt_info = calloc(client->pkt_count, sizeof(struct c_pkt_info));

	if (client->pkt_info == NULL) {
		fprintf(stderr, "myclient ~ init_client(): failed to allocate memory to pkt_info.\n");
		return NULL;
	}

	struct c_pkt_info *pkt;
	for (u_int32_t sn = 0; sn < client->pkt_count; sn++) {
		pkt = &client->pkt_info[sn];

		pkt->active = false;
		pkt->retransmits = 0;
		pkt->ackd = false;
		pkt->file_idx = 0;
	}

	// prepare for handshake
	client->handshake_confirmed = false;
	client->handshake_retransmits = 0;

	// initiate handshake with WR and outfile path
	if (start_handshake(client) < 0) {
		fprintf(stderr, "myclient ~ init_client(): encountered an error while performing handshake with server.\n");
		return NULL;
	}

	return client;
}

// free all memory allocated in client and close infd and sockfd
void free_client(struct client **client) {
	close((*client)->infd);
	close((*client)->sockfd);

	free((*client)->pkt_info);

	free(*client);

	*client = NULL;
}

// TODO: don't log handshake ACK?

// initiate handshake with server, which should respond with the client id
// client id value is put in *client_id
// return 0 on success, -1 on error
// int start_handshake(int sockfd, const char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t *client_id, u_int32_t winsz) {
int start_handshake(struct client *client) {
	if (client == NULL) {
		fprintf(stderr, "myclient ~ start_handshake(): cannot perform handshake with NULL client ptr.\n");
		return -1;
	}

	int recv_res = 1;
	int retransmits = -1;

	do {
		retransmits ++;

		if (retransmits > 3) {
			fprintf(stderr, "Reached max re-transmission limit\n");
			exit(4);
		}

		if (send_wr_pkt(client) < 0) {
			fprintf(stderr, "myclient ~ start_handshake(): failed to send WR pkt to server.\n");
			return -1;
		}

		fprintf(stderr, "Initial write request packet sent.\n");
	} while ((recv_res = recv_server_response(client)) == 1);

	client->id = client->last_ackd_sn;

	fprintf(stderr, "Client ID assigned by server: %u\n", client->id);

	if (recv_res < 0) {
		fprintf(stderr, "myclient ~ start_handshake(): failed to recv server handshake response.\n");
		return -1;
	}

	if (send_ack_pkt(client, client->id) < 0) {
		fprintf(stderr, "myclient ~ start_handshake(): failed to send handshake ACK to server.\n");
		return -1;
	}

	client->start_sn = (client->id + 1) % client->pkt_count;
	printf("handshake completed? start_sn set to %u\n", client->start_sn);

	return 0;
}

int finish_handshake(struct client *client) {
	if (client == NULL) {
		fprintf(stderr, "myclient ~ finish_handshake(): cannot finish handshake with NULL client ptr.\n");
		return -1;
	}

	if (client->last_ackd_sn == client->id) {
		if (send_ack_pkt(client, client->id) < 0) {
			fprintf(stderr, "myclient ~ send_file(): failed to resend handshake ACK.\n");
			return -1;
		}

		client->handshake_retransmits ++;
		if (client->handshake_retransmits > 3) {
			fprintf(stderr, "Reached max re-transmission limit\n");
			exit(4);
		}

		return 1;
	} else {
		client->handshake_confirmed = true;

		fprintf(stderr, "Connection confirmed by server.\n");
	}

	return 0;
}

// send file from fd to sockfd, also using sockaddr
// return 0 on success, -1 on error
// int send_file(int infd, const char *outfile_path, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, u_int32_t winsz) {
int send_file(struct client *client) {
	bool need_pkt_resend, reached_eof = false;

	// need to resend pkts if ack sn < last pkt sn sent
	// send pkts from ack sn + 1
	int pkts_sent;

	int recv_res;

	// continue while eof hasn't been reached or pkts need to be resent
	while (!reached_eof || need_pkt_resend) {
		need_pkt_resend = false;

		do {
			// send pkt window
			if ((pkts_sent = send_window_pkts(client)) < 0) {
				fprintf(stderr, "myclient ~ send_file(): encountered an error while sending pkt window.\n");
				return -1;
			}

			reached_eof = pkts_sent == 0; // update reached_eof

			// wait for server response, to get ack sn
			recv_res = recv_server_response(client);

			if (!client->handshake_confirmed) {
				int handshake_res = finish_handshake(client);
				if (handshake_res == 1) continue;
				if (handshake_res < 0) {
					fprintf(stderr, "myclient ~ send_file(): failed to complete handshake.\n");
					return -1;
				}
			}
		} while (recv_res == 1);

		if (recv_res < 0) {
			fprintf(stderr, "myclient ~ send_file(): encourntered an error while trying to receive server response.\n");
			return -1;
		}

		if (reached_eof) {
			if (client->last_ackd_sn == client->last_sent_sn) {
				if (send_ack_pkt(client, client->id) < 0) {
					fprintf(stderr, "myclient ~ send_file(): failed to send connection termination ACK.\n");
					return -1; // TODO: maybe don't return here?
				}

				break;
			}
		}

		// update pkt info with ack
		u_int32_t sn = client->start_sn;
		struct c_pkt_info *pkt;
		while (sn != (client->last_ackd_sn + 1) % client->pkt_count) {
			pkt = &client->pkt_info[sn];

			if (pkt->active) {
				pkt->ackd = true;
				pkt->active = false;
			}

			sn = (sn + 1) % client->pkt_count;
		}

		need_pkt_resend = client->last_ackd_sn != (client->start_sn + client->winsz) % client->pkt_count;

		client->start_sn = (client->last_ackd_sn + 1) % client->pkt_count;
	}

	return 0;
}

// send window of pkts with content from infd
// returns number of pkts sent, -1 on error
int send_window_pkts(struct client *client) {
	char pkt_buf[client->mss];

	client->start_sn = (client->last_ackd_sn + 1) % client->pkt_count;
	client->last_sent_sn = (client->start_sn + client->pkt_count - 1) % client->pkt_count;

	// reset pkt info array
	if (update_pkt_info(client) < 0) {
		fprintf(stderr, "myclient ~ send_window_pkts(): failed to update pkt info.\n");
		return -1;
	}

	struct c_pkt_info *pkt;

	// check start pkt for ack, if no ack, go back to file idx
	pkt = &client->pkt_info[client->start_sn];
	if (!pkt->ackd && pkt->active) {
		lseek(client->infd, pkt->file_idx, SEEK_SET);
	}

	u_int32_t pyld_sz;

	bool eof_reached = false;

	u_int32_t sn = client->start_sn;
	int pkts_sent = 0;
	int bytes_read;

	printf("send_window_pkts(): start sn is %u\n", sn);

	while ((u_int32_t)pkts_sent < client->winsz && !eof_reached) { // only send max winsz packets
		memset(pkt_buf, 0, sizeof(pkt_buf));

		pkt = &client->pkt_info[sn];

		// update pkts for current transmission
		pkt->file_idx = lseek(client->infd, 0, SEEK_CUR);
		pkt->active = true;
		pkt->ackd = false;

		// bytes_read is our payload size
		bytes_read = read(client->infd, pkt_buf + DATA_HEADER_SIZE, ((u_int32_t)client->mss) - ((u_int32_t)DATA_HEADER_SIZE));
		pyld_sz = (u_int32_t)bytes_read;

		if (bytes_read < 0) {
			fprintf(stderr, "myclient ~ send_window_pkts(): encountered an error reading from infile.\n");
			return -1;
		} else if (bytes_read == 0) {
			eof_reached = true;
		}

		if (send_data_pkt(client, pkt_buf, sizeof(pkt_buf), pyld_sz) < 0) {
			fprintf(stderr, "myclient ~ send_window_pkts(): failed to send DATA pkt to server.\n");
			return -1;
		}

		sn = (sn + 1) % client->pkt_count;

		pkts_sent ++;
	}

	if (eof_reached) {
		fprintf(stderr, "End of file.\n");
		return 0;
	}

	return pkts_sent;
}


int send_pkt(struct client *client, int opcode, char *pkt_buf, size_t pkt_size) {
	if (client == NULL) {
		fprintf(stderr, "myclient ~ send_pkt(): cannot send pkt with NULL client ptr\n");
		return -1;
	}

	if (pkt_buf == NULL) {
		fprintf(stderr, "myclient ~ send_pkt(): cannot send pkt with NULL pkt_buf ptr\n");
		return -1;
	}

	if (assign_pkt_opcode(pkt_buf, opcode) < 0) {
		fprintf(stderr, "myclient ~ send_pkt(): failed to assign opcode to pkt.\n");
		return -1;
	}

	if (sendto(client->sockfd, pkt_buf, pkt_size, 0, &client->serveraddr, client->serveraddr_size) < 0) {
		fprintf(stderr, "myclient ~ send_pkt(): failed to send pkt to server: opcode %u\n", opcode);
		return -1;
	}

	if (log_pkt_sent(client, pkt_buf) < 0) { // TODO: fix
		fprintf(stderr, "myclient ~ send_pkt(): failed to log pkt sent.\n");
		return -1;
	}

	return 0;
}

int send_wr_pkt(struct client *client) {
	if (client == NULL) {
		fprintf(stderr, "myclient ~ send_wr_pkt(): cannot send WR pkt with NULL client ptr.\n");
		return -1;
	}

	// construct WR packet
	char pkt_buf[WR_HEADER_SIZE + strlen(client->outfile_path) + 1];				// null terminated and opcode both 1 byte

	if (assign_wr_winsz(pkt_buf, client->winsz) < 0) {
		fprintf(stderr, "myclient ~ send_wr_pkt(): encountered error assigning window size to handshake buffer.\n");
		return -1;
	}

	memcpy(pkt_buf + WR_HEADER_SIZE, client->outfile_path, strlen(client->outfile_path));	// copy outfile_path to pkt
	pkt_buf[sizeof(pkt_buf) - 1] = 0; // null terminate

	if (send_pkt(client, OP_WR, pkt_buf, sizeof(pkt_buf)) < 0) {
		fprintf(stderr, "myclient ~ send_wr_pkt(): failed to send WR pkt to server.\n");
		return -1;
	}

	return 0;
}

int send_ack_pkt(struct client *client, u_int32_t ack_sn) {
	if (client == NULL) {
		fprintf(stderr, "myclient ~ send_ack_pkt(): cannot send ACK pkt with NULL client ptr.\n");
		return -1;
	}

	// construct ACK packet
	char pkt_buf[ACK_HEADER_SIZE];

	if (assign_ack_sn(pkt_buf, ack_sn) < 0) {
		fprintf(stderr, "myclient ~ send_ack_pkt(): encountered error assigning sn to ACK buffer.\n");
		return -1;
	}

	if (send_pkt(client, OP_ACK, pkt_buf, sizeof(pkt_buf)) < 0) {
		fprintf(stderr, "myclient ~ send_ack_pkt(): failed to send ACK pkt to server.\n");
		return -1;
	}

	client->last_sent_sn = ack_sn;

	return 0;
}

int send_data_pkt(struct client *client, char *pkt_buf, size_t pkt_size, u_int32_t pyld_sz) {
	if (client == NULL) {
		fprintf(stderr, "myclient ~ send_data_pkt(): cannot send DATA pkt with NULL client ptr.\n");
		return -1;
	}

	u_int32_t sn = (client->last_sent_sn + 1) % client->pkt_count;

	// assign client ID
	if (assign_pkt_client_id(pkt_buf, client->id) < 0) {
		fprintf(stderr, "myclient ~ send_data_pkt(): failed to assign client id to DATA pkt.\n");
		return -1;
	}

	// assign sn
	if (assign_pkt_sn(pkt_buf, sn) < 0) {
		fprintf(stderr, "myclient ~ send_data_pkt(): failed to assign sn to DATA buffer.\n");
		return -1;
	}
	
	// assign payload size
	if (assign_pkt_pyld_sz(pkt_buf, pyld_sz) < 0) {
		fprintf(stderr, "myclient ~ send_data_pkt(): failed to assign payload size to DATA pkt.\n");
		return -1;
	}
	
	if (send_pkt(client, OP_DATA, pkt_buf, pkt_size) < 0) {
		fprintf(stderr, "myclient ~ send_data_pkt(): failed to send DATA pkt to server.\n");
		return -1;
	}

	client->last_sent_sn = sn;

	return 0;
}

int update_pkt_info(struct client *client) {
	if (client == NULL) {
		fprintf(stderr, "myclient ~ update_pkt_info(): cannot update pkt info for NULL client ptr.\n");
		return -1;
	}

	struct c_pkt_info *pkt;
	for (u_int32_t sn = 0; sn < client->pkt_count; sn++) {
		pkt = &client->pkt_info[sn];

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

	return 0;
}

// wait for server response, ack_pkt_sn is output
// return 0 on success, 1 on resend, -1 on error
int recv_server_response(struct client *client) {
	if (client == NULL) {
		fprintf(stderr, "myclient ~ recv_server_response(): cannot recv server response with NULL client ptr\n");
		return -1;
	}

	char pkt_buf[MAX_SRVR_RES_SIZE];
	memset(pkt_buf, 0, sizeof(pkt_buf));

	// configure fds and timeout for select() call
	fd_set fds;
	FD_SET(client->sockfd, &fds);

	struct timeval timeout = { LOSS_TIMEOUT_SECS, 0 };

	int bytes_recvd;

	u_int32_t ack_sn;

	if (select(client->sockfd + 1, &fds, NULL, NULL, &timeout) > 0) { // check there is data to be read from socket
		if ((bytes_recvd = recvfrom(client->sockfd, pkt_buf, sizeof(pkt_buf), 0, &client->serveraddr, &client->serveraddr_size)) >= 0) {			
			// recvfrom success
			int opcode = get_pkt_opcode(pkt_buf);
			switch (opcode) {
				case OP_ACK:
					ack_sn = get_ack_sn(pkt_buf);	// assign pkt sn to ack_pkt_sn
					if (ack_sn == 0 && errno == 1) {
						fprintf(stderr, "myclient ~ recv_server_response(): failed to get ACK sn from pkt.\n");
						return -1;
					}

					if (ack_sn == client->last_ackd_sn) return 1; // repeat ACK, last transmission not recvd

					client->last_ackd_sn = ack_sn;
					break;
				case OP_ERROR:
					// TODO: idk how this is supposed to be handled tbh so make sure it's right
					fprintf(stderr, "myclient ~ recv_server_response(): received error from server, exiting.\n");
					return 0;
					break;
				default:
					// TODO: make sure this is implemented correctly too
					fprintf(stderr, "myclient ~ recv_server_response(): unrecognized opcode received from server: %d.\n", opcode);
					return -1;
					break;
			};

			if (log_pkt_recvd(client, pkt_buf) < 0) {
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
int log_pkt_sent(struct client *client, char *pkt_buf) {
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

	char *opstring = opcode == OP_DATA ? "DATA" : "CTRL";

	u_int32_t sn = opcode == OP_WR ? get_wr_sn(pkt_buf) : (opcode == OP_ACK ? get_ack_sn(pkt_buf) : get_data_sn(pkt_buf));
	if (sn == 0 && errno == 1) {
		fprintf(stderr, "myclient ~ log_pkt(): encountered an error getting pkt sn from pkt.\n");
		return -1;
	}

	printf("(sent) %d-%02d-%02dT%02d:%02d:%02dZ, %s, %u, %u, %u, %u\n",	tm->tm_year + 1900,
																	tm->tm_mon + 1,
																	tm->tm_mday,
																	tm->tm_hour,
																	tm->tm_min,
																	tm->tm_sec,
																	opstring,
																	sn,
																	client->start_sn,
																	(sn + 1) % client->pkt_count,
																	(client->start_sn + client->winsz) % (client->pkt_count));
	// fflush(stdout);

	return 0;
}

int log_pkt_recvd(struct client *client, char *pkt_buf) {
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

	printf("(recvd) %d-%02d-%02dT%02d:%02d:%02dZ, %s, %u, %u, %u, %u\n",	tm->tm_year + 1900,
																	tm->tm_mon + 1,
																	tm->tm_mday,
																	tm->tm_hour,
																	tm->tm_min,
																	tm->tm_sec,
																	opstring,
																	sn,
																	client->start_sn,
																	(sn + 1) % client->pkt_count,
																	(client->start_sn + client->winsz) % (client->pkt_count));
	// fflush(stdout);

	return 0;
}
