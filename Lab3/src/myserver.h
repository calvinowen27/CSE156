#ifndef MYSERVER_INCLUDE
#define MYSERVER_INCLUDE

#define START_CLIENTS 5
#define CLIENT_CAP_INCREASE 5

struct client_info;

// receive data from sockfd and echo it back as it arrives back to the client
// this function will run forever once called, or until there is an error (returns -1)
int run(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size);

// initialize client connection with outfile and next client_id, send response to client with client_id
// return 0 on success, -1 on error
int process_write_req(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, char *pkt_buf, struct client_info **clients, uint32_t *max_client_count, uint32_t client_id);

// perform writing actions from a data pkt sent by known client
// if payload size == 0, terminate client connection
// if client unrecognized, don't do anything
// return 0 on success, -1 on error
int process_data_pkt(char *pkt_buf, struct client_info **clients, uint32_t *max_client_count);

#endif
