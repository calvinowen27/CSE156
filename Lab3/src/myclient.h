#ifndef MYCLIENT_INCLUDE
#define MYCLIENT_INCLUDE

#include <stdint.h>

struct pkt_ack_info;

// send file from fd to sockfd, also using sockaddr
// return 0 on success, -1 on error
int send_file(int infd, const char *outfile_path, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, uint32_t winsz);

// initiate handshake with server, which should respond with the client id
// client id value is put in *client_id
// return 0 on success, -1 on error
int perform_handshake(int sockfd, const char *outfile_path, struct sockaddr *sockaddr, socklen_t *sockaddr_size, uint32_t *client_id, uint32_t winsz);

// send window of pkts with content from infd
// returns number of pkts sent, -1 on error
int send_window_pkts(int infd, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, uint32_t winsz, uint32_t client_id, uint32_t start_pkt_sn, struct pkt_ack_info *pkt_idx_pairs);

// wait for server response, ack_pkt_sn is output
// return 0 on success, -1 on error
int recv_server_response(int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, uint32_t *ack_pkt_sn);

// send window of pkts with outfile path as content
// returns number of pkts sent, -1 on error
int send_outfile_path(const char *outfile_path, int *path_idx, int sockfd, struct sockaddr *sockaddr, socklen_t *sockaddr_size, int mss, uint32_t winsz, uint32_t client_id, uint32_t start_pkt_sn, struct pkt_ack_info *pkt_info);

// prints log message of pkt
// returns 0 on success, -1 on error
int log_pkt(char *pkt_buf);

void reset_pkt_info(struct pkt_ack_info *pkt_info, uint32_t winsz);

#endif
