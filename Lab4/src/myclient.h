#ifndef MYCLIENT_INCLUDE
#define MYCLIENT_INCLUDE

struct c_pkt_info {
	off_t file_idx;
	bool ackd;
	int retransmits;
	bool active;
};

struct server_info {
	const char *ip;
	int port;
};

struct client {
	int infd;
	int sockfd;

	struct server_info server;

	const char *outfile_path;

	struct sockaddr serveraddr;
	socklen_t serveraddr_size;

	int mss;
	u_int32_t id;
	u_int32_t winsz;
	u_int32_t pkt_count;

	u_int32_t start_sn;
	u_int32_t last_sent_sn;
	u_int32_t last_ackd_sn;

	struct c_pkt_info *pkt_info;

	bool handshake_confirmed;
	int handshake_retransmits;
};

// initialize client with relevant information, perform handshake with server
// return pointer to client struct on success, NULL on failure
struct client *init_client(const char *infile_path, const char *outfile_path, struct server_info server, int mss, u_int32_t winsz);

// free all memory allocated in client and close infd and sockfd
void free_client(struct client **client);

// send file from client fd to sockfd
// return 0 on success, -1 on error
int send_file(struct client *client);
// int send_file(int infd, const char *outfile_path, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, u_int32_t winsz);

// initiate handshake with server, which should respond with the client id
// client id value is put in *client_id
// return 0 on success, -1 on error
int start_handshake(struct client *client);

int finish_handshake(struct client *client);

// send window of pkts with content from infd
// returns number of pkts sent, -1 on error
int send_window_pkts(struct client *client);

int send_pkt(struct client *client, int opcode, char *pkt_buf, size_t pkt_size);

int send_wr_pkt(struct client *client);

int send_ack_pkt(struct client *client, u_int32_t ack_sn);

int send_data_pkt(struct client *client, char *pkt_buf, size_t pkt_size, u_int32_t pyld_sz);

int update_pkt_info(struct client *client);

// wait for server response, ack_pkt_sn is output
// return 0 on success, -1 on error
int recv_server_response(struct client *client);

// prints log message of pkt
// returns 0 on success, -1 on error
int log_pkt_sent(struct client *client, char *pkt_buf);

int log_pkt_recvd(struct client *client, char *pkt_buf);

struct server_info *parse_serv_conf(const char *serv_conf_path, int servn);

#endif
