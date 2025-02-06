#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

#include "client_info.h"
#include "utils.h"

// initialize client array and set default values for client_info entries
// return pointer to client array of length max_client_count, or NULL on error
struct client_info *init_clients(u_int32_t max_client_count) {
	struct client_info *clients = calloc(sizeof(struct client_info), max_client_count);

	if (clients == NULL) {
		fprintf(stderr, "myserver ~ init_clients(): encountered an error initializing client array.\n");
		return NULL;
	}

	for (u_int32_t i = 0; i < max_client_count; i++) {
		struct client_info *client = &(clients[i]);
		client->is_active = false;
		client->outfile_path = NULL;
	}

	return clients;
}

// reallocate client array with inc additional entries, initialize new entries
// set new value of max_client_count, and set *clients to new ptr
// return 0 on success, -1 on error
int increase_client_cap(struct client_info **clients, u_int32_t *max_client_count, u_int32_t inc) {
	if (clients == NULL || *clients == NULL) {
		fprintf(stderr, "myserver ~ increase_client_cap(): invalid ptr passed to clients parameter.\n");
		return -1;
	}

	if (max_client_count == NULL) {
		fprintf(stderr, "myserver ~ increase_client_cap(): null ptr passed to max_client_count.\n");
		return -1;
	}

	u_int32_t new_client_count = *max_client_count + inc;
	struct client_info *new_clients = realloc(*clients, new_client_count);
	
	if (new_clients == NULL) {
		fprintf(stderr, "myserver ~ increase_client_cap(): encountered an error reallocating client array to size %u.\n", new_client_count);
		return -1;
	}

	// init new client spaces
	for (u_int32_t i = *max_client_count; i < new_client_count; i++) {
		struct client_info *client = &(*clients)[i];
		client->is_active = false;
		client->outfile_path = NULL;
	}

	*max_client_count = new_client_count;
	*clients = new_clients;

	return 0;
}

// accept new client with id client_id writing to file outfile_path
// open outfile and add client to clients with outfd
// return 0 on success, -1 on failure
int accept_client(struct client_info **clients, u_int32_t *max_client_count, u_int32_t client_id, char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t winsz) {
	if (clients == NULL || *clients == NULL) {
		fprintf(stderr, "myserver ~ accept_client(): invalid ptr passed to clients parameter.\n");
		return -1;
	}

	if (max_client_count == NULL) {
		fprintf(stderr, "myserver ~ accept_client(): null ptr passed to max_client_count.\n");
		return -1;
	}
	
	// find first inactive client slot in clients
	bool inactive_client_found = false;

	for (u_int32_t i = 0; i < *max_client_count; i++) {
		struct client_info *client = &(*clients)[i];
		if (!client->is_active) {
			inactive_client_found = true;
			if (client_info_init(client, client_id, outfile_path, sockaddr, sockaddr_size, winsz) < 0) {
				fprintf(stderr, "myserver ~ accept_client(): encountered error while initializing client_info.\n");
				return -1;
			}
			
			break;
		}
	}

	// if no inactive clients, allocate space for more then init new client
	if (!inactive_client_found) {
		u_int32_t inactive_idx = *max_client_count;

		if (increase_client_cap(clients, max_client_count, MAX_CLIENTS_INCREASE) < 0) {
			fprintf(stderr, "myserver ~ accept_client(): encountered error increasing client cap.\n");
			return -1;
		}

		struct client_info *client = &(*clients)[inactive_idx];
		if (client_info_init(client, client_id, outfile_path, sockaddr, sockaddr_size, winsz) < 0) {
			fprintf(stderr, "myserver ~ accept_client(): encountered error while initializing client_info.\n");
			return -1;
		}
	}

	return 0;
}

// initialize client_info with all relevant fields, allocate ooo buffers, open outfile
// return 0 on success, -1 on error
int client_info_init(struct client_info *client, u_int32_t client_id, char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t winsz) {
	if (client == NULL) {
		fprintf(stderr, "myserver ~ client_info_init(): cannot initialize client with null ptr.\n");
		return -1;
	}

	// create outfile directories if necessary
	if (create_file_directory(outfile_path) < 0) {
		fprintf(stderr, "myserver ~ client_info_init(): failed to create outfile directories.\n");
		return -1;
	}

	// open outfile
	int outfd = open(outfile_path, O_CREAT | O_TRUNC | O_RDWR, 0664);

	// set client_info values
	client->is_active = true;
	client->id = client_id;
	client->expected_sn = 0;
	client->outfd = outfd;
	client->outfile_path = outfile_path;
	client->first_unwritten_sn = 0;
	client->sockaddr = sockaddr;
	client->sockaddr_size = sockaddr_size;
	client->winsz = winsz;

	// allocate pkt_info buffer
	client->pkt_win = calloc(sizeof(struct pkt_info), client->winsz);
	if (client->pkt_win == NULL) {
		fprintf(stderr, "myserver ~ client_info_init(): encountered an error initializing client pkt_win.\n");
		return -1;	
	}

	for (u_int32_t sn = 0; sn < client->winsz; sn++) {
		struct pkt_info *pkt_info = &client->pkt_win[sn];

		pkt_info->written = false;
		pkt_info->file_idx = 0;
	}

	client->expected_start_sn = 0;

	return 0;
}

// terminate connection with client with id client_id and free necessary memory
// close outfile and set client inactive
// return 0 on success, -1 on error
int terminate_client(struct client_info **clients, u_int32_t *max_client_count, u_int32_t client_id) {
	if (clients == NULL || *clients == NULL) {
		fprintf(stderr, "myserver ~ terminate_client(): invalid ptr passed to clients parameter.\n");
		return -1;
	}

	if (max_client_count == NULL) {
		fprintf(stderr, "myserver ~ terminate_client(): null ptr passed to max_client_count.\n");
		return -1;
	}

	bool client_found = false;

	// find client in array and free all allocated heap memory, close outfile
	for (u_int32_t i = 0; i < *max_client_count; i++) {
		struct client_info *client = &(*clients)[i];
		if (client->id == client_id) {
			client->is_active = false;
			// free(client->ooo_file_idxs);
			// free(client->ooo_pkt_sns);
			free(client->pkt_win);
			free(client->outfile_path);
			close(client->outfd);
			client_found = true;
			break;
		}
	}

	if (!client_found) {
		fprintf(stderr, "myserver ~ terminate_client(): could not find client with id %u to terminate.\n", client_id);
		return -1;
	}

	return 0;
}
