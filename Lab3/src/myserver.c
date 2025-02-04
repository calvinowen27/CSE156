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
#include "client_info.h"

// #define IP_ADDR "127.0.0.1"
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

	if (run(sockfd, (struct sockaddr *)&clientaddr, &clientaddr_size) < 0) {
		fprintf(stderr,"myserver ~ main(): server failed to receive from socket.\n");
	}

	close(sockfd);

	return 0;
}

// run server: accept pkts and send acks for highest pkt sn from client during breaks
// this function will run forever once called, or until there is an error (returns -1)
int run(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size) {
	// while res > 0
	//		if select (data available)
	//			res = recv_data()
	//			write data to file
	//		else
	//			send acks
	//			reset sn maps (client id:highest sn)

	// init pkt buffer
	char *pkt_buf = malloc(sizeof(char) * BUFFER_SIZE);

	uint32_t max_client_count = START_CLIENTS;

	// initialize clients
	struct client_info *clients = init_clients(max_client_count);
	if (clients == NULL) {
		fprintf(stderr, "myserver ~ run(): encountered error initializing clients.\n");
		return -1;
	}


	// configure fds and timeout for select() call
	fd_set fds;
	FD_SET(sockfd, &fds);

	struct timeval timeout = { LOSS_TIMEOUT_SECS, 0 };

	uint32_t next_client_id = 1;

	int bytes_recvd;

	while (1) { // hopefully run forever
		// reset timeout
		timeout.tv_sec = LOSS_TIMEOUT_SECS;
		FD_SET(sockfd, &fds);

		printf("waiting\n");
		if (select(sockfd + 1, &fds, NULL, NULL, &timeout) > 0) { // check there is data to be read from socket
			printf("see pkt\n");
			// data available at socket
			// read into buffer
			if ((bytes_recvd = recvfrom(sockfd, pkt_buf, BUFFER_SIZE, 0, sockaddr, sockaddr_size)) >= 0) {
				int opcode = get_pkt_opcode(pkt_buf);
				
				switch (opcode) {
					case OP_WR:
						if (process_write_req(sockfd, sockaddr, sockaddr_size, pkt_buf, &clients, &max_client_count, next_client_id) < 0) {
							fprintf(stderr, "myserver ~ run(): encountered error processing write request.\n");
							return -1;
						}

						next_client_id++;
						break;
					case OP_DATA:
						if (process_data_pkt(pkt_buf, &clients, &max_client_count) < 0) {
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
			uint32_t pkts_counted;

			// send acks and reset id:sn maps
			for (uint32_t i = 0; i < max_client_count; i++) {
				struct client_info *client = &clients[i];
				if (!client->is_active) {
					continue;
				}

				// find first unackd pkt
				pkts_counted = 0;
				client->first_unackd_sn = client->expected_start_sn;
				while (pkts_counted < client->winsz) {
					if (!client->ooo_pkts[client->first_unackd_sn].ackd) {
						break;
					}

					client->first_unackd_sn ++;

					if (client->first_unackd_sn == client->winsz) client->first_unackd_sn = 0;

					pkts_counted ++;
				}

				client->expected_start_sn = client->first_unackd_sn;

				// next expected is first unackd
				client->expected_sn = client->expected_start_sn;

				char ack_buf[5];
				ack_buf[0] = OP_ACK;
				if (assign_ack_sn(ack_buf, client->first_unackd_sn == 0 ? client->winsz - 1 : client->first_unackd_sn - 1) < 0) {
					fprintf(stderr, "myserver ~ run(): encountered an error assigning client %u lowest sn %u to ack buf.\n", client->id, client->first_unackd_sn);
					return -1;
				}

				if (sendto(sockfd, ack_buf, 5, 0, client->sockaddr, *client->sockaddr_size) < 0) {
					fprintf(stderr, "myserver ~ run(): encountered an error sending ack to client %u with sn %u.\n", client->id, client->first_unackd_sn);
					return -1;
				}

				printf("Sent ack to client %u for sn %u.\n", client->id, client->first_unackd_sn - 1);
			}
		}
	}

	// free all heap memory
	for (uint32_t i = 0; i < max_client_count; i++) {
		// free(clients[i].ooo_file_idxs);
		// free(clients[i].ooo_pkt_sns);
		free(clients[i].ooo_pkts);
	}

	free(pkt_buf);

	fprintf(stderr, "myserver ~ run(): something went wrong. Closing server.\n");
	return -1; // TODO: check if exit code is needed
}

// initialize client connection with outfile and next client_id, send response to client with client_id
// return 0 on success, -1 on error
int process_write_req(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, char *pkt_buf, struct client_info **clients, uint32_t *max_client_count, uint32_t client_id) {
	if (clients == NULL || *clients == NULL) {
		fprintf(stderr, "myserver ~ process_write_req(): invalid ptr passed to clients parameter.\n");
		return -1;
	}

	if (max_client_count == NULL) {
		fprintf(stderr, "myserver ~ process_write_req(): null ptr passed to max_client_count.\n");
		return -1;
	}

	uint32_t winsz = get_write_req_winsz(pkt_buf);
	if (winsz == 0) {
		fprintf(stderr, "myserver ~ process_write_req(): encountered an error getting window size from write request pkt.\n");
		return -1;
	}

	printf("winsz: %u\n", winsz);
	
	// create outfile path, starting at byte 1 of pkt_buf (byte 0 is opcode)
	char *outfile_path = calloc(strlen(pkt_buf + WR_HEADER_SIZE) + 1, sizeof(char));
	memcpy(outfile_path, pkt_buf + WR_HEADER_SIZE, strlen(pkt_buf + WR_HEADER_SIZE));

	// accept client, initializing all client_info data and opening outfile for writing
	if (accept_client(clients, max_client_count, client_id, outfile_path, sockaddr, sockaddr_size, winsz) < 0) {
		fprintf(stderr, "myserver ~ process_write_req(): encountered error while accepting client %u.\n", client_id);
		return -1;
	}

	// create response buffer with ACK opcode and client_id in rest of bytes
	char res_buf[1 + CID_BYTES];
	res_buf[0] = OP_ACK;

	uint8_t *cid_bytes = split_bytes(client_id);
	if (cid_bytes == NULL) {
		fprintf(stderr, "myserver ~ process_write_req(): something went wrong splitting client_id bytes.\n");
		return -1;
	}

	res_buf[1] = cid_bytes[0];
	res_buf[2] = cid_bytes[1];
	res_buf[3] = cid_bytes[2];
	res_buf[4] = cid_bytes[3];

	free(cid_bytes);

	// send ack with client id back to client
	if (sendto(sockfd, res_buf, sizeof(res_buf), 0, sockaddr, *sockaddr_size) < 0) {
		fprintf(stderr, "myserver ~ process_write_req(): failed to send connection acceptance pkt to client.\n");
		return -1;
	}

	return 0;
}

// perform writing actions from a data pkt sent by known client
// if payload size == 0, terminate client connection
// if client unrecognized, don't do anything
// return 0 on success, -1 on error
int process_data_pkt(char *pkt_buf, struct client_info **clients, uint32_t *max_client_count) {
	// if payload size == 0: terminate connection
	// if pkt in client ooo buffer, write to file based on that
	// else write to end of file
	// if client unrecognized, idk don't do it

	printf("data pkt recvd.\n");

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
	uint32_t client_id = get_data_client_id(pkt_buf);
	if (client_id == 0) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting client_id from pkt.\n");
		return -1;
	}

	struct client_info *client = NULL;
	for (uint32_t i = 0; i < *max_client_count; i++) {
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
	uint32_t pkt_sn = get_data_sn(pkt_buf);
	if (pkt_sn == 0 && errno == EDEVERR) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting pkt sn from data pkt.\n");
		return -1;
	}

	if (client->first_unackd_sn == pkt_sn) {
		client->first_unackd_sn++;
	}

	// get payload size, terminate client connection if == 0xffffffff
	uint32_t pyld_sz = get_data_pyld_sz(pkt_buf);
	if (pyld_sz == 0xffffffff) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting payload size from data pkt.\n");
		return -1;
	} else if (pyld_sz == 0) {
		printf("myserver ~ Payload size of 0 encountered. Terminating connection with client %u.\n", client_id);
		if (terminate_client(clients, max_client_count, client_id) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error terminating connection with client %u.\n", client_id);
			return -1;
		}
	}

	char idk[pyld_sz+1];
	idk[pyld_sz] = 0;
	memcpy(idk, pkt_buf + DATA_HEADER_SIZE, pyld_sz);
	printf("\nPacket %u:\n%s\n", pkt_sn, idk);

	// if we've made it to here, everything is valid and client is writing data to file

	// write based on sn, check for ooo
	if (pkt_sn == client->expected_sn) { // normal, write bytes to outfile
		if (write_n_bytes(client->outfd, pkt_buf + DATA_HEADER_SIZE, pyld_sz) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error writing bytes to outfile: %s.\n", strerror(errno));
			return -1;
		}

		client->expected_sn = pkt_sn + 1;
	} else if (pkt_sn > client->expected_sn) { // pkt received before expected, update ooo buffer and write to end of file
		// store all pkts between expected_sn and pkt_sn in ooo_buffer
		for (uint32_t sn = client->expected_sn; sn < pkt_sn; sn++) {
			// save ooo pkt to buffer at current file location
			struct ooo_pkt *ooo_pkt = &client->ooo_pkts[sn];
			ooo_pkt->ackd = false;
			ooo_pkt->file_idx = lseek(client->outfd, 0, SEEK_END);
		}

		// write bytes of current pkt to end of file
		if (write_n_bytes(client->outfd, pkt_buf + DATA_HEADER_SIZE, pyld_sz) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error writing bytes to outfile: %s.\n", strerror(errno));
			return -1;
		}

		client->expected_sn = pkt_sn + 1;
	} else { // pkt received late, write based on ooo buffer
		struct ooo_pkt *ooo_pkt = &client->ooo_pkts[pkt_sn];
		ooo_pkt->ackd = true;
		
		// go to correct location in outfile
		off_t file_idx = lseek(client->outfd, ooo_pkt->file_idx, SEEK_SET);

		if (shift_file_contents(client->outfd, file_idx, pyld_sz) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error shifting file contents for OOO write.\n");
			return -1;
		}

		// write bytes to outfile
		if (write_n_bytes(client->outfd, pkt_buf + DATA_HEADER_SIZE, pyld_sz) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error writing bytes to outfile: %s\n", strerror(errno));
			return -1;
		}
	}

	lseek(client->outfd, 0, SEEK_END); // reset seek ptr in case more packets come in

	return 0;
}
