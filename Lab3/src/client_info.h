#ifndef CLIENT_INFO_INCLUDE
#define CLIENT_INFO_INCLUDE

#define MAX_CLIENTS_INCREASE 5

struct ooo_pkt {
	uint32_t sn;
	off_t file_idx;
	bool ackd;
};

struct client_info {
	uint32_t id;
	int outfd;
	struct ooo_pkt *ooo_pkts;
	uint32_t num_ooo_pkts;
	uint32_t ooo_pkt_max_count;
	uint32_t lowest_unackd_sn;
	struct sockaddr *sockaddr;
	socklen_t *sockaddr_size;
	uint32_t winsz;
	uint32_t expected_sn;
	bool is_active;
	char *outfile_path;	// only saving this so it can be freed
};

// initialize client array and set default values for client_info entries
// return pointer to client array of length *max_client_count, or NULL on error
struct client_info *init_clients(uint32_t max_client_count);

// reallocate client array with inc additional entries, initialize new entries
// set new value of max_client_count, and set *clients to new ptr
// return 0 on success, -1 on error
int increase_client_cap(struct client_info **clients, uint32_t *max_client_count, uint32_t inc);

// accept new client with id client_id writing to file outfile_path
// return 0 on success, -1 on error
int accept_client(struct client_info **clients, uint32_t *max_client_count, uint32_t client_id, char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, uint32_t winsz);

// initialize client_info with all relevant fields, allocate ooo buffers, open outfile
// return 0 on success, -1 on error
int client_info_init(struct client_info *client, uint32_t client_id, char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, uint32_t winsz);

// terminate connection with client with id client_id and free necessary memory
// close outfile and set client inactive
// return 0 on success, -1 on error
int terminate_client(struct client_info **clients, uint32_t *max_client_count, uint32_t client_id);

#endif
