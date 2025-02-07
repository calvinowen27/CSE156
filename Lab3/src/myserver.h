#ifndef MYSERVER_INCLUDE
#define MYSERVER_INCLUDE

#define START_CLIENTS 5
#define CLIENT_CAP_INCREASE 5

struct client_info;

// receive data from sockfd and echo it back as it arrives back to the client
// this function will run forever once called, or until there is an error (returns -1)
int run(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int droppc);

int process_ack_pkt(char *pkt_buf, struct client_info **clients, u_int32_t *max_client_count);

// initialize client connection with outfile and next client_id, send response to client with client_id
// return 0 on success, -1 on error
int process_write_req(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, char *pkt_buf, struct client_info **clients, u_int32_t *max_client_count, u_int32_t client_id, int *pkts_sent, int *pkts_recvd, int droppc);

int complete_handshake(int sockfd, char *res_buf, struct sockaddr *sockaddr, socklen_t *sockaddr_size, char *pkt_buf, u_int32_t client_id, int *pkts_sent, int *pkts_recvd, int droppc);

// perform writing actions from a data pkt sent by known client
// if payload size == 0, terminate client connection
// if client unrecognized, don't do anything
// return 0 on success, -1 on error
int process_data_pkt(int sockfd, char *pkt_buf, struct client_info **clients, u_int32_t *max_client_count, int *pkts_sent, int droppc);

// send ack to client based on what packets were received
// return 0 on success, -1 on error
int send_client_ack(struct client_info *client, int sockfd, int *pkts_sent, int droppc);

// determines wether to drop a pkt based on pkt_count
// prints log message if pkt is dropped
// returns 1 if true, 0 if false
int drop_pkt(char *pkt_buf, int *pkt_count, int droppc);

#endif
