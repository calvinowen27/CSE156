#ifndef MYCLIENT_INCLUDE
#define MYCLIENT_INCLUDE

struct pkt_ack_info;

// send file from fd to sockfd, also using sockaddr
// return 0 on success, -1 on error
int send_file(int infd, const char *outfile_path, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, u_int32_t winsz);

// initiate handshake with server, which should respond with the client id
// client id value is put in *client_id
// return 0 on success, -1 on error
int perform_handshake(int sockfd, const char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t *client_id, u_int32_t winsz);

// send window of pkts with content from infd
// returns number of pkts sent, -1 on error
int send_window_pkts(int infd, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, u_int32_t winsz, u_int32_t client_id, u_int32_t start_pkt_sn, struct pkt_ack_info *pkt_idx_pairs);

// wait for server response, ack_pkt_sn is output
// return 0 on success, -1 on error
int recv_server_response(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, u_int32_t *ack_pkt_sn);

// prints log message of pkt
// returns 0 on success, -1 on error
int log_pkt(char *pkt_buf);

#endif
