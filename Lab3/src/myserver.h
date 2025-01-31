#ifndef MYSERVER_INCLUDE
#define MYSERVER_INCLUDE

#define START_CLIENTS 5
#define CLIENT_CAP_INCREASE 5

struct client_info;

// receive data from sockfd and echo it back as it arrives back to the client
// this function will run forever once called, or until there is an error (returns -1)
int run(int sockfd, struct sockaddr *sockaddr, socklen_t *sock_size);

int process_write_req(int sockfd, char *pkt_buf, struct client_info **clients, uint32_t *client_count, uint32_t client_id);

// initialize client array and set default values for client_info entries
// return pointer to client array of length *client_count, or NULL on error
struct client_info *init_clients(uint32_t *client_count);

// reallocate client array with CLIENT_CAP_INCREASE additional entries, initialize new entries
// set new value of client_count, and set *clients to new ptr
// return 0 on success, -1 on error
int increase_client_cap(struct client_info **clients, uint32_t *client_count);

// accept new client with id client_id writing to file outfile_path
// return 0 on success, -1 on error
int accept_client(struct client_info **clients, uint32_t *client_count, uint32_t client_id, const char *outfile_path);

// initialize client_info with all relevant fields, allocate ooo buffers, open outfile
// return 0 on success, -1 on error
int client_info_init(struct client_info *client, uint32_t client_id, const char *outfile_path);

// terminate connection with client with id client_id and free necessary memory
// close outfile and set client inactive
// return 0 on success, -1 on error
int terminate_client(struct client_info **clients, uint32_t *client_count, uint32_t client_id);

#endif
