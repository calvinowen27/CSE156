#ifndef MYCLIENT_INCLUDE
#define MYCLIENT_INCLUDE

struct c_pkt_info {
	off_t file_idx;
	bool ackd;
	int retransmits;
	bool active;
};

struct client {
	int infd;
	int sockfd;

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
};

// initialize client with relevant information, perform handshake with server
// return pointer to client struct on success, NULL on failure
struct client *init_client(const char *infile_path, const char *outfile_path, const char *server_ip, int server_port, int mss, u_int32_t winsz);

// free all memory allocated in client and close infd and sockfd
void free_client(struct client **client);

// send file from client fd to sockfd
// return 0 on success, -1 on error
int send_file(struct client *client);
// int send_file(int infd, const char *outfile_path, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, u_int32_t winsz);

// initiate handshake with server, which should respond with the client id
// client id value is put in *client_id
// return 0 on success, -1 on error
int perform_handshake(int sockfd, const char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t *client_id, u_int32_t winsz);

// send window of pkts with content from infd
// returns number of pkts sent, -1 on error
int send_window_pkts(int infd, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, u_int32_t winsz, u_int32_t client_id, u_int32_t start_pkt_sn, struct c_pkt_info *pkt_idx_pairs, u_int32_t *last_sent_sn);

int send_server_pkt(struct client *client, int opcode, char *pkt_buf, size_t pkt_size);

// wait for server response, ack_pkt_sn is output
// return 0 on success, -1 on error
int recv_server_response(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t *ack_pkt_sn, u_int32_t start_sn, u_int32_t winsz);

int recv_pkt(struct client *client, char *pkt_buf);

// prints log message of pkt
// returns 0 on success, -1 on error
int log_pkt(char *pkt_buf, u_int32_t start_sn, u_int32_t winsz);

#endif
