#ifndef CLIENT_INFO_INCLUDE
#define CLIENT_INFO_INCLUDE

#define MAX_CLIENTS_INCREASE 5

struct pkt_info {
	off_t file_idx;
	bool written;
	bool ackd;
};

struct client_info {
	u_int32_t id;

	char *outfile_path;	// only saving this so it can be freed
	int outfd;

	u_int32_t winsz;
	u_int32_t pkt_count;
	struct pkt_info *pkt_info;
	u_int32_t expected_sn;
	u_int32_t expected_start_sn;

	struct sockaddr *sockaddr;
	socklen_t *sockaddr_size;
	
	bool ack_sent;
	bool is_active;
};

// initialize client array and set default values for client_info entries
// return pointer to client array of length *max_client_count, or NULL on error
struct client_info *init_clients(u_int32_t max_client_count);

// reallocate client array with inc additional entries, initialize new entries
// set new value of max_client_count, and set *clients to new ptr
// return 0 on success, -1 on error
int increase_client_cap(struct client_info **clients, u_int32_t *max_client_count, u_int32_t inc);

// accept new client with id client_id writing to file outfile_path
// return 0 on success, -1 on error
int accept_client(struct client_info **clients, u_int32_t *max_client_count, u_int32_t client_id, char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t winsz);

// initialize client_info with all relevant fields, allocate ooo buffers, open outfile
// return 0 on success, -1 on error
int client_info_init(struct client_info *client, u_int32_t client_id, char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t winsz);

// terminate connection with client with id client_id and free necessary memory
// close outfile and set client inactive
// return 0 on success, -1 on error
int terminate_client(struct client_info **clients, u_int32_t *max_client_count, u_int32_t client_id);

#endif
