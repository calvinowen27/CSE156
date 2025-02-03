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
	if (argc != 2) {
		printf("Invalid number of options provided.\nThis is where I will print the usage of the program.\n");
		exit(1);
	}

	int port = atoi(argv[1]);
	if (port < 0 || port > 65535) {
		printf("Invalid port provided. Please provide a port between 0-65535.\n");
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

	int bytes_recvd, res = 1;

	while (res == 0) { // hopefully run forever
		// reset timeout
		timeout.tv_sec = LOSS_TIMEOUT_SECS;

		if (select(sockfd + 1, &fds, NULL, NULL, &timeout) > 0) { // check there is data to be read from socket
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
						// write to client:outfile pkt data, check ooo buffer to see if file idx stored
						// if payload size == 0: terminate connection
						break;
					case OP_ERROR:
						// terminate connection
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
		}
	}

	// free all heap memory
	for (uint32_t i = 0; i < max_client_count; i++) {
		free(clients[i].ooo_file_idxs);
		free(clients[i].ooo_pkt_sns);
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
	
	// create outfile path, starting at byte 1 of pkt_buf (byte 0 is opcode)
	char *outfile_path = calloc(strlen(pkt_buf + 1) + 1, sizeof(char));
	memcpy(outfile_path, pkt_buf + 1, strlen(pkt_buf + 1));

	// accept client, initializing all client_info data and opening outfile for writing
	if (accept_client(clients, max_client_count, client_id, outfile_path) < 0) {
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

	// get client id from pkt and check if we are serving that client
	uint32_t client_id = get_data_client_id(pkt_buf);
	if (client_id == 0) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting client_id from pkt.\n");
		return -1;
	}

	bool client_found = false;
	for (uint32_t i = 0; i < *max_client_count; i++) {
		if ((*clients)[i].id == client_id) {
			client_found = true;
			break;
		}
	}

	if (!client_found) {
		fprintf(stderr, "myserver ~ process_data_pkt(): failed to find client %u. Terminating.\n", client_id);
		return -1;
	}

	// get pkt sn
	uint32_t pkt_sn = get_data_sn(pkt_buf);
	if (pkt_sn == 0) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting pkt sn from data pkt.\n");
		return -1;
	}

	// get payload size, terminate client connection if == 0
	uint32_t pyld_sz = get_data_pyld_sz(pkt_buf);
	if (pyld_sz == 0xffffffff) {
		fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error getting payload size from data pkt.\n");
		return -1;
	} else if (pyld_sz == 0) {
		printf("myserver ~ Paylod size of 0 encountered. Terminating connection with client %u.\n", client_id);
		if (terminate_client(clients, max_client_count, client_id) < 0) {
			fprintf(stderr, "myserver ~ process_data_pkt(): encountered an error terminating connection with client %u.\n", client_id);
			return -1;
		}

		return 0;
	}

	// if we've made it to here, everything is valid and client is writing data to file

	// write based on sn, check for ooo
}

// TODO: rework for server side (copied from myclient Lab2)
// void parse_pkt() {
// 	packet_id_recvd = (uint8_t)buf[0];
// 	if (packet_id_recvd == expected_packet_id_recv) {
// 		// write bytes to outfile
// 		if (write_n_bytes(outfd, buf+HEADER_SIZE, strlen(buf+HEADER_SIZE)) < 0) {
// 			fprintf(stderr, "myclient ~ send_recv_file(): encountered error writing bytes to outfile\n");
// 			fprintf(stderr, "%s\n", strerror(errno));
// 			return -1;
// 		}

// 		// normal, increment expected packet ID
// 		expected_packet_id_recv = packet_id_recvd + 1;
// 	} else if (packet_id_recvd > expected_packet_id_recv) { // packet recvd too soon
// 		// store all packets in between recvd and expected into ooo buffer with file locations
// 		for (uint8_t id = expected_packet_id_recv; id < packet_id_recvd; id++) {
// 			ooo_packet_ids[num_packets_not_recvd] = id;
// 			ooo_packet_locations[num_packets_not_recvd] = lseek(outfd, 0, SEEK_END);
// 			num_packets_not_recvd += 1;
// 		}

// 		// write bytes to outfile
// 		if (write_n_bytes(outfd, buf+HEADER_SIZE, strlen(buf+HEADER_SIZE)) < 0) {
// 			fprintf(stderr, "myclient ~ send_recv_file(): encountered error writing bytes to outfile\n");
// 			fprintf(stderr, "%s\n", strerror(errno));
// 			return -1;
// 		}

// 		// increment expected packet ID
// 		expected_packet_id_recv = packet_id_recvd + 1;
// 	} else { // packet recvd late
// 		// write packet data to file depending on ooo buffer file location
// 		// pop packet data from ooo buffers
// 		// change all ooo packet file locations with larger ID to current position of seek ptr
// 		for (int i = 0; i < num_packets_not_recvd; i++) {
// 			if (ooo_packet_ids[i] == packet_id_recvd) {
// 				// go to correct location in outfile
// 				off_t file_idx = lseek(outfd, ooo_packet_locations[i], SEEK_SET);

// 				if (shift_file_contents(outfd, file_idx, strlen(buf+HEADER_SIZE)) < 0) {
// 					fprintf(stderr, "myclient ~ send_recv_file(): encountered error shifting file contents for OOO write.\n");
// 					return -1;
// 				}

// 				// write bytes to outfile
// 				if (write_n_bytes(outfd, buf+HEADER_SIZE, strlen(buf+HEADER_SIZE)) < 0) {
// 					fprintf(stderr, "myclient ~ send_recv_file(): encountered error writing bytes to outfile\n");
// 					fprintf(stderr, "%s\n", strerror(errno));
// 					return -1;
// 				}
				
// 				// shift packet info down in buffer
// 				if (i < num_packets_not_recvd - 1) {
// 					ooo_packet_ids[i] = ooo_packet_ids[i+1];
// 					ooo_packet_locations[i] = lseek(outfd, 0, SEEK_CUR);
// 				} else {
// 					ooo_packet_ids[i] = 0;
// 					ooo_packet_locations[i] = 0;
// 				}
// 			} else if (ooo_packet_ids[i] > packet_id_recvd) {
// 				// shift packet info down in buffer
// 				if (i < num_packets_not_recvd - 1) {
// 					ooo_packet_ids[i] = ooo_packet_ids[i+1];
// 					ooo_packet_locations[i] = lseek(outfd, 0, SEEK_CUR);
// 				} else {
// 					ooo_packet_ids[i] = 0;
// 					ooo_packet_locations[i] = 0;
// 					num_packets_not_recvd -= 1;
// 				}
// 			}
// 		}
		
// 		lseek(outfd, 0, SEEK_END); // reset seek ptr in case more packets come in

// 		// expected not recvd yet, leave value the same
// 	}
// }
