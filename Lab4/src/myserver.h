#ifndef MYSERVER_INCLUDE
#define MYSERVER_INCLUDE

#define START_CLIENTS 5
#define CLIENT_CAP_INCREASE 5

struct client_info;

struct server_info {
	int sockfd;
	struct sockaddr clientaddr, serveraddr;
	socklen_t clientaddr_size;
	int droppc;
	int pkts_recvd;
	int pkts_sent;
	struct client_info *clients;
	u_int32_t max_client_count;
};

// initialize server info with port and droppc, init socket and clients
// returns pointer to server_info struct on success, NULL on failure
struct server_info *init_server(int port, int droppc);

// free allocated memory for server, terminate clients, and close socket
void close_server(struct server_info **server);

// accept new client with id client_id writing to file outfile_path
// open outfile and add client to clients with outfd
// return client_info ptr on success, NULL on failure
struct client_info *accept_client(struct server_info *server, char *outfile_path, u_int32_t winsz);

// terminate connection with client with id client_id and free necessary memory
// close outfile and set client inactive
// return 0 on success, -1 on error
int terminate_client(struct server_info *server, u_int32_t client_id);

// receive data from sockfd and echo it back as it arrives back to the client
// this function will run forever once called, or until there is an error (returns -1)
int run(struct server_info *server);

// send pkt to client
// return 0 on success, -1 on error
int send_pkt(struct server_info *server, struct client_info *client, char *pkt_buf, size_t pkt_size);

// send ack to client based on what packets were received
// return 0 on success, -1 on error
int send_client_ack(struct server_info *server, struct client_info *client);

// send ack to client with given sn
// return 0 on success, -1 on error
int send_client_ack_sn(struct server_info *server, struct client_info *client, u_int32_t ack_sn);

// update ack and written status of pkts when ack is sent
// return 0 on success, -1 on failure
int update_pkt_info(struct client_info *client);

// finds sn for client ack based on first unwritten pkt
u_int32_t get_client_ack_sn(struct client_info *client);

// recv pkt from socket into pkt_buf
// pkt will potentially be dropped
// return 1 on success, 2 on select timeout, 0 on drop, and -1 on error
int recv_pkt(struct server_info *server, char *pkt_buf);

// process pkt from pkt_buf based on opcode
// return 0 on success, -1 on error
int process_pkt(struct server_info *server, char *pkt_buf);

// initialize client connection with outfile and next client_id, send response to client with client_id
// return 0 on success, -1 on error
int process_write_req(struct server_info *server, char *pkt_buf);

// perform writing actions from a data pkt sent by known client
// if payload size == 0, terminate client connection
// if client unrecognized, don't do anything
// return 0 on success, -1 on error
int process_data_pkt(struct server_info *server, char *pkt_buf);

// process ack pkt from client, for initializing or terminating connection
// return 0 on success, -1 on error
int process_ack_pkt(struct server_info *server, char *pkt_buf);

// determines wether to drop a pkt based on pkt_count
// prints log message if pkt is dropped
// returns 1 if true, 0 if false
int drop_pkt(char *pkt_buf, int *pkt_count, int droppc);

#endif
