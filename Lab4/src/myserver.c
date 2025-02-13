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
#include <math.h>

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

	struct server_info *server = init_server(port, droppc);
	if (server == NULL) {
		fprintf(stderr, "myserver ~ main(): encountered error initializing server state.\n");
		exit(1); // TODO
	}

	if (run(server) < 0) {
		fprintf(stderr,"myserver ~ main(): server failed to receive from socket.\n");

		close_server(&server);

		exit(1);
	}

	close_server(&server);

	return 0;
}

// initialize server info with port and droppc, init socket and clients
// returns pointer to server_info struct on success, NULL on failure
struct server_info *init_server(int port, int droppc) {
	struct server_info *server = malloc(sizeof(struct server_info));

	server->sockfd = init_socket((struct sockaddr_in *)&server->serveraddr, NULL, port, AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
	if (server->sockfd < 0) {
		fprintf(stderr, "myserver ~ init_server(): server init_socket() failed.\n");
		return NULL;
	}

	server->clientaddr_size = sizeof(server->clientaddr);

	server->droppc = droppc;
	server->pkts_recvd = 0;
	server->pkts_sent = 0;

	server->max_client_count = START_CLIENTS;

	// initialize clients
	server->clients = init_clients(server->max_client_count);
	if (server->clients == NULL) {
		fprintf(stderr, "myserver ~ init_server(): encountered error initializing clients.\n");
		return NULL;
	}

	// server->next_client_id = 1;

	return server;
}

// free allocated memory for server, terminate clients, and close socket
void close_server(struct server_info **server) {
	struct client_info *client;
	for (u_int32_t i = 0; i < (*server)->max_client_count; i++) {
		client = &(*server)->clients[i];
		if (client->is_active) {
			terminate_client(*server, client->id);
		}
	}

	free((*server)->clients);
	
	close((*server)->sockfd);

	free(*server);

	*server = NULL;
}

// accept new client with id client_id writing to file outfile_path
// open outfile and add client to clients with outfd
// return client_info ptr on success, NULL on failure
struct client_info *accept_client(struct server_info *server, char *outfile_path, u_int32_t winsz) {
	if (server == NULL) {
		fprintf(stderr, "myserver ~ accept_client(): NULL ptr passed to server parameter.\n");
		return NULL;
	}

	if (outfile_path == NULL) {
		fprintf(stderr, "myserver ~ accept_client(): NULL ptr passed to outfile_path parameter.\n");
		return NULL;
	}
	
	// find first inactive client slot in clients
	bool inactive_client_found = false;

	struct client_info *client;
	for (u_int32_t id = 1; id <= server->max_client_count; id++) {
		client = &server->clients[id - 1];
		if (!client->is_active) {
			inactive_client_found = true;
			if (client_info_init(client, id, outfile_path, server->clientaddr, winsz) < 0) {
				fprintf(stderr, "myserver ~ accept_client(): encountered error while initializing client_info.\n");
				return NULL;
			}

			// client->expected_start_sn = (client->id + 1) % client->pkt_count;
			
			break;
		}
	}

	// if no inactive clients, allocate space for more then init new client
	if (!inactive_client_found) {
		u_int32_t id = server->max_client_count + 1;

		if (increase_client_cap(&server->clients, &server->max_client_count, MAX_CLIENTS_INCREASE) < 0) {
			fprintf(stderr, "myserver ~ accept_client(): encountered error increasing client cap.\n");
			return NULL;
		}

		client = &server->clients[id - 1];
		if (client_info_init(client, id, outfile_path, server->clientaddr, winsz) < 0) {
			fprintf(stderr, "myserver ~ accept_client(): encountered error while initializing client_info.\n");
			return NULL;
		}
	}

	return client;
}

// terminate connection with client with id client_id and free necessary memory
// close outfile and set client inactive
// return 0 on success, -1 on error
int terminate_client(struct server_info *server, u_int32_t client_id) {
	if (client_id < 1 || client_id > server->max_client_count) {
		fprintf(stderr, "myserver ~ terminate_client(): invalid client_id %u, max client id is %u\n", client_id, server->max_client_count);
		return -1;
	}

	struct client_info *client = &server->clients[client_id - 1];
	if (client == NULL) {
		fprintf(stderr, "myserver ~ terminate_client(): invalid ptr passed to client parameter.\n");
		return -1;
	}

	if (client->is_active) {
		// reset values and free allocated memory
		client->is_active = false;
		free(client->pkt_info);
		free(client->outfile_path);
		close(client->outfd);
	} else {
		fprintf(stderr, "myserver ~ terminate_client(): cannot terminate inactive client %u.\n", client_id);
		return -1;
	}

	fprintf(stderr, "Client %u terminated.\n", client->id);

	return 0;
}

// run server: accept pkts and send acks for highest pkt sn from client during breaks
// this function will run forever once called, or until there is an error (returns -1)
int run(struct server_info *server) {
// int run(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int droppc) {
	// init pkt buffer
	char pkt_buf[BUFFER_SIZE];

	int recv_res;

	while (1) { // hopefully run forever
		if ((recv_res = recv_pkt(server, pkt_buf)) == 1) {
			// pkt recvd successfully
			if (process_pkt(server, pkt_buf) < 0) {
				fprintf(stderr, "myserver ~ run(): encountered error processing pkt.\n");
				return -1;
			}

			memset(pkt_buf, 0, BUFFER_SIZE);
		} else if (recv_res == 2) {
			// select timeout, send ACKs
			struct client_info *client;
			for (u_int32_t i = 0; i < server->max_client_count; i++) {
				client = &server->clients[i];
				if (!client->is_active) {
					continue;
				}

				if (send_client_ack(server, client) < 0) {
					fprintf(stderr, "myserver ~ run(): encountered error while sending ack to client.\n");
					return -1;
				}
			}
		} else if (recv_res < 0) {
			// error
			fprintf(stderr, "myserver ~ run(): encountered error receiving pkt with recv_pkt() call.\n");
			return -1;
		}
		// continue otherwise (0)
	}

	return -1; // TODO: check if exit code is needed
}

// send pkt to client
// return 0 on success, -1 on error
int send_pkt(struct server_info *server, struct client_info *client, char *pkt_buf, size_t pkt_size) {
	if (drop_pkt(pkt_buf, &server->pkts_sent, server->droppc)) {
		return 0;
	}

	if (sendto(server->sockfd, pkt_buf, pkt_size, 0, &client->sockaddr, client->sockaddr_size) < 0) {
		fprintf(stderr, "myserver ~ send_pkt(): encountered an error sending pkt to client %u.\n", client->id);
		return -1;
	}

	return 0;
}

// send ack to client based on what packets were received
// return 0 on success, -1 on error
int send_client_ack(struct server_info *server, struct client_info *client) {
	u_int32_t ack_sn = get_client_ack_sn(client);

	if (send_client_ack_sn(server, client, ack_sn) < 0) {
		fprintf(stderr, "myserver ~ send_client_ack(): encountered error sending ACK...\n");
		return -1;
	}

	return 0;
}

// send ack to client with given sn
// return 0 on success, -1 on error
int send_client_ack_sn(struct server_info *server, struct client_info *client, u_int32_t ack_sn) {
	char ack_buf[5];
	ack_buf[0] = OP_ACK;

	if (assign_ack_sn(ack_buf, ack_sn) < 0) {
		fprintf(stderr, "myserver ~ send_client_ack_sn(): encountered an error assigning ACK sn %u to client %u ACK.\n", ack_sn, client->id);
		return -1;
	}

	if (send_pkt(server, client, ack_buf, sizeof(ack_buf)) < 0) {
		fprintf(stderr, "myserver ~ send_client_ack_sn(): encountered error sending ACK %u to client %u.\n", ack_sn, client->id);
		return -1;
	}

	if (update_pkt_info(client) < 0) {
		fprintf(stderr, "myserver ~ send_client_ack_sn(): encountered error updating pkt info.\n");
		return -1;
	}

	client->ack_sent = true;
	client->expected_start_sn = (ack_sn + 1) % client->pkt_count;
	client->expected_sn = client->expected_start_sn;

	return 0;
}

// update ack and written status of pkts when ack is sent
// return 0 on success, -1 on failure
int update_pkt_info(struct client_info *client) {
	if (client == NULL || !client->is_active) {
		fprintf(stderr, "myserver ~ update_pkt_info(): can't update pkt info for inactive client.\n");
		return -1;
	}

	u_int32_t sn = (client->expected_start_sn + 1) % client->pkt_count, pkts = 0;
	struct pkt_info *pkt;
	while (pkts < client->winsz) {
		pkt = &client->pkt_info[sn];

		if (!pkt->written) break;

		pkt->ackd = true;
		pkt->written = false;

		client->pkt_info[sn].ackd = false;

		sn = (sn + 1) % client->pkt_count;
		pkts ++;
	}

	return 0;
}

// finds sn for client ack based on first unwritten pkt
u_int32_t get_client_ack_sn(struct client_info *client) {
	if (client->ack_sent) return (client->expected_start_sn + client->pkt_count - 1) % client->pkt_count;

	u_int32_t first_unwritten_sn = client->expected_start_sn;

	// find first unackd pkt
	u_int32_t pkts_counted = 0;
	first_unwritten_sn = client->expected_start_sn;
	struct pkt_info *pkt;
	while (pkts_counted < client->winsz) {
		pkt = &client->pkt_info[first_unwritten_sn];
		
		if (!pkt->written) {
			break;
		}

		pkt->written = false;
		pkt->ackd = true;

		client->pkt_info[(first_unwritten_sn + client->winsz) % client->pkt_count].ackd = false;
		
		first_unwritten_sn = (first_unwritten_sn + 1) % client->pkt_count;

		pkts_counted ++;
	}

	client->expected_start_sn = first_unwritten_sn;

	// next expected is first unackd
	client->expected_sn = client->expected_start_sn;

	return (first_unwritten_sn + client->pkt_count - 1) % client->pkt_count;
}

// recv pkt from socket into pkt_buf
// pkt will potentially be dropped
// return 1 on success, 2 on select timeout, 0 on drop, and -1 on error
int recv_pkt(struct server_info *server, char *pkt_buf) {
	fd_set fds;
	FD_SET(server->sockfd, &fds);

	struct timeval timeout = { LOSS_TIMEOUT_SECS, 0 };

	if (select(server->sockfd + 1, &fds, NULL, NULL, &timeout) > 0) { // check there is data to be read from socket
		// data available at socket
		// read into buffer
		if (recvfrom(server->sockfd, pkt_buf, BUFFER_SIZE, 0, &server->clientaddr, &server->clientaddr_size) >= 0) {

			// pkt dropped
			if (drop_pkt(pkt_buf, &server->pkts_recvd, server->droppc)) {
				return 0;
			}

			return 1; // pkt recvd successfully
		} else { // recvfrom() failure
			// TODO: check this
			fprintf(stderr, "myserver ~ recv_pkt(): encountered error with recvfrom() call.\n");
			return -1;
		}
	} else {
		return 2;
	}
}

// process pkt from pkt_buf based on opcode
// return 0 on success, -1 on error
int process_pkt(struct server_info *server, char *pkt_buf) {
	int opcode = get_pkt_opcode(pkt_buf);
	
	switch (opcode) {
		case OP_WR:
			if (process_write_req(server, pkt_buf) < 0) {
				fprintf(stderr, "myserver ~ process_pkt(): encountered error processing write request.\n");
				return -1;
			}
			break;
		case OP_DATA:
			if (process_data_pkt(server, pkt_buf) < 0) {
				fprintf(stderr, "myserver ~ process_pkt(): encountered error processing data pkt.\n");
				return -1;
			}
			break;
		case OP_ACK:
			if (process_ack_pkt(server, pkt_buf) < 0) {
				fprintf(stderr, "myserver ~ process_pkt(): encountered error processing ack pkt.\n");
				return -1;
			}
			break;
		default:
			// do nothing?
			break;
	}

	return 0;
}

// initialize client connection with outfile and next client_id, send response to client with client_id
// return 0 on success, -1 on error
int process_write_req(struct server_info *server, char *pkt_buf) {
	if (server == NULL) {
		fprintf(stderr, "myserver ~ process_write_req(): null ptr passed to server.\n");
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

	// accept client, initializing all client_info data and opening outfile for writing
	struct client_info *client = accept_client(server, outfile_path, winsz);
	if (client == NULL) {
		fprintf(stderr, "myserver ~ process_write_req(): encountered error while accepting client.\n");
		return -1;
	}

	// send ack with client id as sn
	if (send_client_ack_sn(server, client, client->id) < 0) {
		fprintf(stderr, "myserver ~ process_write_req(): encountered error sending ACK with client id %u to accept client.\n", client->id);
		return -1;
	}

	client->handshaking = true;

	return 0;
}

// perform writing actions from a data pkt sent by known client
// if payload size == 0, terminate client connection
// if client unrecognized, don't do anything
// return 0 on success, -1 on error
int process_data_pkt(struct server_info *server, char *pkt_buf) {
	// if payload size == 0: terminate connection
	// if pkt in client ooo buffer, write to file based on that
	// else write to end of file
	// if client unrecognized, idk don't do it

	if (server == NULL) {
		fprintf(stderr, "myserver ~ process_data_pkt(): NULL ptr passed to server parameter.\n");
		return -1;
	}

	if (pkt_buf == NULL) {
		fprintf(stderr, "myserver ~ process_data_pkt(): NULL ptr passed to pkt_buf parameter.\n");
		return -1;
	}

	// get client id from pkt and check if we are serving that client
	u_int32_t client_id = get_data_client_id(pkt_buf);
	if (client_id == 0) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting client_id from pkt.\n");
		return -1;
	}

	if (client_id < 1 || client_id > server->max_client_count) {
		fprintf(stderr, "myserver ~ process_data_pkt(): invalid client_id provided: %u, max client_id is %u\n", client_id, server->max_client_count);
	}

	struct client_info *client = &server->clients[client_id - 1];

	// don't process data, but don't exit server
	if (client == NULL || !client->is_active) {
		fprintf(stderr, "myserver ~ process_data_pkt(): failed to find client %u. Terminating.\n", client_id);
		return 0;
	}

	if (client->handshaking) {
		// resend handshake ACK (not supposed to get data yet)
		if (send_client_ack_sn(server, client, client->id) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error resending client %u handshake ACK.\n", client->id);
			return -1;
		}

		return 0;
	}

	// get pkt sn
	u_int32_t pkt_sn = get_data_sn(pkt_buf);
	if (pkt_sn == 0 && errno == 1) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting pkt sn from data pkt.\n");
		return -1;
	}

	struct pkt_info *pkt = &client->pkt_info[pkt_sn];

	// get payload size, terminate client connection if == 0
	u_int32_t pyld_sz = get_data_pyld_sz(pkt_buf);
	if (pyld_sz == 0 && errno == 1) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting payload size from data pkt.\n");
		return -1;
	} else if (pyld_sz == 0 && pkt_sn == client->expected_sn) {
		client->ack_sent = false;
		client->terminating = true;
		pkt->written = true;

		// ack final pkt to finish terminating client
		fprintf(stderr, "myserver ~ Payload size of 0 encountered. Terminating connection with client %u.\n", client_id);
		if (send_client_ack_sn(server, client, pkt_sn) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error sending final ack to client.\n");
			return -1;
		}

		return 0;
	}

	// if we've made it to here, everything is valid and client is writing data to file

	if (pkt_sn == client->expected_sn) { // normal, write bytes to outfile
		client->ack_sent = false;

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

		client->expected_sn = (pkt_sn + 1) % client->pkt_count;
	} else {
		// ACK last received valid pkt (whatever was before expected sn)
		u_int32_t ack_sn = (client->expected_sn + client->pkt_count - 1) % client->pkt_count;
		if (send_client_ack_sn(server, client, ack_sn) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error sending ack to client.\n");
			return -1;
		}
	}

	// check if last pkt for fast ACK, instead of waiting for timeout
	if (pkt_sn == (client->expected_start_sn + client->winsz - 1) % client->pkt_count) {
		if (send_client_ack_sn(server, client, pkt_sn) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered error sending ack to client.\n");
			return -1;
		}
	}

	return 0;
}

// process ack pkt from client, for initializing or terminating connection
// return 0 on success, -1 on error
int process_ack_pkt(struct server_info *server, char *pkt_buf) {
	if (server == NULL) {
		fprintf(stderr, "myserver ~ process_ack_pkt(): NULL ptr passed to server parameter.\n");
		return -1;
	}

	if (pkt_buf == NULL) {
		fprintf(stderr, "myserver ~ process_ack_pkt(): invalid ptr passed to pkt_buf parameter.\n");
		return -1;
	}

	u_int32_t client_id = get_ack_sn(pkt_buf);
	if (client_id == 0) {
		fprintf(stderr, "myserver ~ process_ack_pkt(): encountered error getting client id from ACK.\n");
		return -1;
	}

	if (client_id < 1 || client_id > server->max_client_count) {
		fprintf(stderr, "myserver ~ process_ack_pkt(): invalid client id contained in ACK: %u.\n", client_id);
		return -1;
	}

	struct client_info *client = &server->clients[client_id - 1];
	if (client == NULL || !client->is_active) {
		fprintf(stderr, "myserver ~ process_ack_pkt(): cannot process ack for inactive client %u.\n", client_id);
		return -1;
	}

	if (client->terminating) {
		if (terminate_client(server, client_id) < 0) {
			fprintf(stderr, "myserver ~ process_ack_pkt(): encountered an error terminating connection with client %u.\n", client_id);
			return -1;
		}
	} else if (client->handshaking) {
		client->handshaking = false;
	}

	return 0;
}

// TODO: distribute drops?

// determines wether to drop a pkt based on pkt_count
// prints log message if pkt is dropped
// returns 1 if true, 0 if false
int drop_pkt(char *pkt_buf, int *pkt_count, int droppc) {

	int every = 100 / droppc;

	if (((*pkt_count) % 100) % every != 0 || fabs(((float)((*pkt_count) % 100) / ((float)100 / (float)droppc)) - every) <= ((float)100 / (float)droppc) - every || droppc == 0) {
		(*pkt_count) ++;

		return 0;
	}

	(*pkt_count) ++;

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

	char *opstring = opcode == OP_ACK ? "DROP ACK" : (opcode == OP_WR ? "DROP CTRL" : "DROP DATA");

	u_int32_t sn = opcode == OP_WR ? 0 : (opcode == OP_ACK ? get_ack_sn(pkt_buf) : get_data_sn(pkt_buf));
	if (sn == 0 && errno == 1) {
		fprintf(stderr, "myserver ~ drop_recvd_pkt(): encountered an error getting pkt sn from pkt.\n");
		return -1;
	}

	printf("%d-%02d-%02dT%02d:%02d:%02dZ, %s, %u\n", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, opstring, sn);
	fflush(stdout);

	return 1;
}
