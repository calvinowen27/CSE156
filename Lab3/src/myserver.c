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
#include <time.h>
#include <stdint.h>

#include "myserver.h"
#include "utils.h"
#include "protocol.h"
#include "client_info.h"

#define BUFFER_SIZE 65535

int main(int argc, char **argv) {
	// handle command line args
	if (argc != 3) {
		printf("Invalid number of options provided.\nThis is where I will print the usage of the program.\n");
		exit(1);
	}

	int port = atoi(argv[1]);
	if (port < 0 || port > 65535) {
		printf("Invalid port provided. Please provide a port between 0-65535.\n");
		exit(1);
	}

	int droppc = atoi(argv[2]);
	if (droppc < 0 || droppc > 100) {
		printf("Invalid droppc provided. Please provide a drop percentage between 0-100.\n");
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

	if (run(sockfd, (struct sockaddr *)&clientaddr, &clientaddr_size, droppc) < 0) {
		fprintf(stderr,"myserver ~ main(): server failed to receive from socket.\n");
	}

	close(sockfd);

	return 0;
}

// TODO: make sure WR pkt outfile path works with low mss

// run server: accept pkts and send acks for highest pkt sn from client during breaks
// this function will run forever once called, or until there is an error (returns -1)
int run(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int droppc) {
	// while res > 0
	//		if select (data available)
	//			res = recv_data()
	//			write data to file
	//		else
	//			send acks
	//			reset sn maps (client id:highest sn)

	// init pkt buffer
	char pkt_buf[BUFFER_SIZE];

	u_int32_t max_client_count = START_CLIENTS;

	// initialize clients
	struct client_info *clients = init_clients(max_client_count);
	if (clients == NULL) {
		fprintf(stderr, "myserver ~ run(): encountered error initializing clients.\n");
		return -1;
	}

	int pkts_recvd = 0, pkts_sent = 0;

	// configure fds and timeout for select() call
	fd_set fds;
	FD_SET(sockfd, &fds);

	struct timeval timeout = { LOSS_TIMEOUT_SECS, 0 };

	u_int32_t next_client_id = 1;

	int bytes_recvd;

	while (1) { // hopefully run forever
		// reset timeout
		timeout.tv_sec = LOSS_TIMEOUT_SECS;
		FD_SET(sockfd, &fds);

		if (select(sockfd + 1, &fds, NULL, NULL, &timeout) > 0) { // check there is data to be read from socket
			// data available at socket
			// read into buffer
			if ((bytes_recvd = recvfrom(sockfd, pkt_buf, BUFFER_SIZE, 0, sockaddr, sockaddr_size)) >= 0) {

				if (drop_pkt(pkt_buf, pkts_recvd, droppc)) {
					pkts_recvd ++;
					continue;
				}

				pkts_recvd ++;

				int opcode = get_pkt_opcode(pkt_buf);
				
				switch (opcode) {
					case OP_WR:
						if (process_write_req(sockfd, sockaddr, sockaddr_size, pkt_buf, &clients, &max_client_count, next_client_id, &pkts_sent, &pkts_recvd, droppc) < 0) {
							fprintf(stderr, "myserver ~ run(): encountered error processing write request.\n");
							return -1;
						}

						next_client_id++;
						break;
					case OP_DATA:
						if (process_data_pkt(sockfd, pkt_buf, &clients, &max_client_count, &pkts_sent, droppc) < 0) {
							fprintf(stderr, "myserver ~ run(): encountered error processing data pkt.\n");
							return -1;
						}
						break;
					default:
						// do nothing?
						break;
				}

				memset(pkt_buf, 0, BUFFER_SIZE);
			} else { // recvfrom() failure
				fprintf(stderr, "myserver ~ run(): encountered error with recvfrom() call.\n");
				return -1;
			}
		} else {
			// send acks and reset id:sn maps
			for (u_int32_t i = 0; i < max_client_count; i++) {
				struct client_info *client = &clients[i];
				if (!client->is_active) {
					continue;
				}

				if (send_client_ack(client, sockfd, &pkts_sent, droppc) < 0) {
					fprintf(stderr, "myserver ~ run(): encountered error while sending ack to client.\n");
					return -1;
				}
			}
		}
	}

	// free all heap memory
	for (u_int32_t i = 0; i < max_client_count; i++) {
		// free(clients[i].ooo_file_idxs);
		// free(clients[i].ooo_pkt_sns);
		free(clients[i].pkt_win);
	}

	fprintf(stderr, "myserver ~ run(): something went wrong. Closing server.\n");
	return -1; // TODO: check if exit code is needed
}

// send ack to client based on what packets were received
// return 0 on success, -1 on error
int send_client_ack(struct client_info *client, int sockfd, int *pkts_sent, int droppc) {
	// find first unackd pkt
	u_int32_t pkts_counted = 0;
	client->first_unwritten_sn = client->expected_start_sn;
	struct pkt_info *pkt;
	while (pkts_counted < client->winsz) {
		pkt = &client->pkt_win[client->first_unwritten_sn];
		// fprintf(stderr, "pkt %u written: %s\n", client->first_unwritten_sn, pkt->written ? "true" : "false");
		
		if (!pkt->written) {
			break;
		}

		pkt->written = false;

		client->first_unwritten_sn ++;

		if (client->first_unwritten_sn == client->winsz) client->first_unwritten_sn = 0;

		pkts_counted ++;
	}

	client->expected_start_sn = client->first_unwritten_sn;

	// next expected is first unackd
	client->expected_sn = client->expected_start_sn;

	char ack_buf[5];
	ack_buf[0] = OP_ACK;
	u_int32_t ack_sn = client->first_unwritten_sn == 0 ? client->winsz - 1 : client->first_unwritten_sn - 1;
	if (assign_ack_sn(ack_buf, ack_sn) < 0) {
		fprintf(stderr, "myserver ~ send_client_ack(): encountered an error assigning client %u lowest sn %u to ack buf.\n", client->id, client->first_unwritten_sn);
		return -1;
	}

	if (drop_pkt(ack_buf, *pkts_sent, droppc)) {
		(*pkts_sent) ++;

		return 0;
	} else if (sendto(sockfd, ack_buf, 5, 0, client->sockaddr, *client->sockaddr_size) < 0) {
		fprintf(stderr, "myserver ~ send_client_ack(): encountered an error sending ack to client %u with sn %u.\n", client->id, client->first_unwritten_sn);
		return -1;
	}

	(*pkts_sent) ++;
	
	return 0;
}

// initialize client connection with outfile and next client_id, send response to client with client_id
// return 0 on success, -1 on error
int process_write_req(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, char *pkt_buf, struct client_info **clients, u_int32_t *max_client_count, u_int32_t client_id, int *pkts_sent, int *pkts_recvd, int droppc) {
	if (clients == NULL || *clients == NULL) {
		fprintf(stderr, "myserver ~ process_write_req(): invalid ptr passed to clients parameter.\n");
		return -1;
	}

	if (max_client_count == NULL) {
		fprintf(stderr, "myserver ~ process_write_req(): null ptr passed to max_client_count.\n");
		return -1;
	}

	u_int32_t winsz = get_write_req_winsz(pkt_buf);
	if (winsz == 0) {
		fprintf(stderr, "myserver ~ process_write_req(): encountered an error getting window size from write request pkt.\n");
		return -1;
	}

	// create outfile path, starting at byte 1 of pkt_buf (byte 0 is opcode)
	char *outfile_path = calloc(strlen(pkt_buf + WR_HEADER_SIZE) + 1, sizeof(char));
	memcpy(outfile_path, pkt_buf + WR_HEADER_SIZE, strlen(pkt_buf + WR_HEADER_SIZE));

	// create response buffer with ACK opcode and client_id in rest of bytes
	char res_buf[1 + CID_BYTES];
	res_buf[0] = OP_ACK;

	if (assign_ack_sn(res_buf, client_id) < 0) {
		fprintf(stderr, "myserver ~ process_write_req(): encountered error assigning client ID bytes to response pkt.\n");
		return -1;
	}

	if (complete_handshake(sockfd, res_buf, sockaddr, sockaddr_size, pkt_buf, clients, max_client_count, client_id, pkts_sent, pkts_recvd, droppc) < 0) {
		fprintf(stderr, "myserver ~ process_write_req(): encountered error completing handshake with client %u.\n", client_id);
		return -1;
	}

	// accept client, initializing all client_info data and opening outfile for writing
	if (accept_client(clients, max_client_count, client_id, outfile_path, sockaddr, sockaddr_size, winsz) < 0) {
		fprintf(stderr, "myserver ~ process_write_req(): encountered error while accepting client %u.\n", client_id);
		return -1;
	}

	return 0;
}

int complete_handshake(int sockfd, char *res_buf, struct sockaddr *sockaddr, socklen_t *sockaddr_size, char *pkt_buf, struct client_info **clients, u_int32_t *max_client_count, u_int32_t client_id, int *pkts_sent, int *pkts_recvd, int droppc) {
	// configure fds and timeout for select() call
	fd_set fds;
	FD_SET(sockfd, &fds);

	struct timeval timeout = { LOSS_TIMEOUT_SECS, 0 };

	int bytes_recvd;

	u_int32_t ack_sn = 0;
	int retransmits = 0;

	while (ack_sn != client_id && retransmits <= 3) {
		// reset timeout
		timeout.tv_sec = LOSS_TIMEOUT_SECS;
		FD_SET(sockfd, &fds);

		if (drop_pkt(pkt_buf, pkts_sent, droppc)) {
			pkts_sent ++;
			continue;
		}

		// send ack with client id back to client
		if (sendto(sockfd, res_buf, sizeof(res_buf), 0, sockaddr, *sockaddr_size) < 0) {
			fprintf(stderr, "myserver ~ complete_handshake(): failed to send connection acceptance pkt to client.\n");
			return -1;
		}

		if (select(sockfd + 1, &fds, NULL, NULL, &timeout) > 0) { // check there is data to be read from socket
			memset(pkt_buf, 0, sizeof(pkt_buf));

			// data available at socket
			// read into buffer
			if ((bytes_recvd = recvfrom(sockfd, pkt_buf, BUFFER_SIZE, 0, sockaddr, sockaddr_size)) >= 0) {

				if (drop_pkt(pkt_buf, pkts_recvd, droppc)) {
					pkts_recvd ++;
					continue;
				}

				if ((ack_sn = get_ack_sn(pkt_buf)) == 0 && errno == 1) {
					fprintf(stderr, "myserver ~ complete_handshake(): encountered an error reading ACK sn from handshake response.\n");
					retransmits ++;
				}
			}
		} else {
			retransmits ++;
		}
	}

	if (retransmits > 3) {
		fprintf(stderr, "myserver ~ complete_handshake(): exceeded 3 retransmissions for handshake. terminating client.\n");
		if (terminate_client(clients, max_client_count, client_id) < 0) {
			fprintf(stderr, "my server ~ complete_handshake(): encountered error terminating client %u.\n");
			return -1;
		}
	}

	return 0;
}

// perform writing actions from a data pkt sent by known client
// if payload size == 0, terminate client connection
// if client unrecognized, don't do anything
// return 0 on success, -1 on error
int process_data_pkt(int sockfd, char *pkt_buf, struct client_info **clients, u_int32_t *max_client_count, int *pkts_sent, int droppc) {
	// if payload size == 0: terminate connection
	// if pkt in client ooo buffer, write to file based on that
	// else write to end of file
	// if client unrecognized, idk don't do it

	if (clients == NULL || *clients == NULL) {
		fprintf(stderr, "myserver ~ process_data_pkt(): invalid ptr passed to clients parameter.\n");
		return -1;
	}

	if (max_client_count == NULL) {
		fprintf(stderr, "myserver ~ process_data_pkt(): null ptr passed to max_client_count.\n");
		return -1;
	}

	if (pkt_buf == NULL) {
		fprintf(stderr, "myserver ~ process_data_pkt(): invalid ptr passed to pkt_buf parameter.\n");
		return -1;
	}

	// get client id from pkt and check if we are serving that client
	u_int32_t client_id = get_data_client_id(pkt_buf);
	if (client_id == 0) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting client_id from pkt.\n");
		return -1;
	}

	struct client_info *client = NULL;
	for (u_int32_t i = 0; i < *max_client_count; i++) {
		if ((*clients)[i].id == client_id) {
			client = &(*clients)[i];
			break;
		}
	}

	// don't process data, but don't exit server
	if (client == NULL) {
		fprintf(stderr, "myserver ~ process_data_pkt(): failed to find client %u. Terminating.\n", client_id);
		return 0;
	}

	// get pkt sn
	u_int32_t pkt_sn = get_data_sn(pkt_buf);
	if (pkt_sn == 0 && errno == EDEVERR) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting pkt sn from data pkt.\n");
		return -1;
	}

	// get payload size, terminate client connection if == 0
	u_int32_t pyld_sz = get_data_pyld_sz(pkt_buf);
	if (pyld_sz == 0xffffffff) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting payload size from data pkt.\n");
		return -1;
	} else if (pyld_sz == 0) {
		fprintf(stderr, "myserver ~ Payload size of 0 encountered. Terminating connection with client %u.\n", client_id);
		if (send_client_ack(client, sockfd, pkts_sent, droppc) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error sending final ack to client.\n");
			return -1;
		}

		if (terminate_client(clients, max_client_count, client_id) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error terminating connection with client %u.\n", client_id);
			return -1;
		}

		return 0;
	}

	// if we've made it to here, everything is valid and client is writing data to file
	struct pkt_info *pkt = &client->pkt_win[pkt_sn];

	// write based on sn, check for ooo
	if (pkt_sn == client->expected_sn) { // normal, write bytes to outfile
		if (pkt->written) {
			lseek(client->outfd, pkt->file_idx, SEEK_SET);
		} else {
			pkt->file_idx = lseek(client->outfd, 0, SEEK_END);
		}

		if (write_n_bytes(client->outfd, pkt_buf + DATA_HEADER_SIZE, pyld_sz) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error writing bytes to outfile: %s.\n", strerror(errno));
			return -1;
		}

		pkt->written = true;

		client->expected_sn = (pkt_sn + 1) % client->winsz;
	} else {
		if (send_client_ack(client, sockfd, pkts_sent, droppc) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error sending ack to client.\n");
			return -1;
		}
	}

	if (pkt_sn == client->expected_start_sn - 1 || (pkt_sn == client->winsz - 1 && client->expected_start_sn == 0)) {
		if (send_client_ack(client, sockfd, pkts_sent, droppc) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error sending ack to client.\n");
			return -1;
		}
	}

	return 0;
}

// determines wether to drop a pkt based on pkt_count
// prints log message if pkt is dropped
// returns 1 if true, 0 if false
int drop_pkt(char *pkt_buf, int pkt_count, int droppc) {
	if ((pkt_count % 100) + 1 > droppc) {
		return 0;
	}

	time_t t = time(NULL);
	struct tm *tm = gmtime(&t);

	u_int32_t opcode = get_pkt_opcode(pkt_buf);
	if (opcode == 0) {
		fprintf(stderr, "myserver ~ drop_recvd_pkt(): encountered error getting opcode from pkt.\n");
		return -1;
	}

	if (opcode < OP_WR || opcode > OP_DATA) {
		fprintf(stderr, "myserver ~ drop_recvd_pkt(): opcode %u is not supported by server.\n", opcode);
		return -1;
	}

	char *opstring = opcode == OP_ACK ? "DROP ACK" : (OP_WR ? "DROP CTRL" : "DROP DATA");

	u_int32_t sn = opcode == OP_WR ? 0 : (opcode == OP_ACK ? get_ack_sn(pkt_buf) : get_data_sn(pkt_buf));
	if (sn == 0 && errno == EDEVERR) {
		fprintf(stderr, "myserver ~ drop_recvd_pkt(): encountered an error getting pkt sn from pkt.\n");
		return -1;
	}

	printf("%d-%02d-%02dT%02d:%02d:%02dZ, %s, %u\n", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, opstring, sn);

	return 1;
}
