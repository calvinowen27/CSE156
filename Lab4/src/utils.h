#ifndef UTILS_INCLUDE
#define UTILS_INCLUDE

#include <stdbool.h>

struct sockaddr_in;

void logerr(const char *);

// split uint32_t into uint8_t[4]
// must free pointer when done using it
u_int8_t *split_bytes(u_int32_t val);

// reuinite uint8_t[4] into uin32_t
u_int32_t reunite_bytes(u_int8_t *bytes);

// read into buf from sockfd. reads n bytes or the size of buf, whichever is smaller
// return bytes read on success, -1 for error
int read_n_bytes(int fd, char *buf, int n);

// write n bytes from buf into sockfd
// returns bytes written on success, -1 on error
int write_n_bytes(int sockfd, char *buf, int n);

// continually read bytes from infd and write them to outfd, until n bytes have been passed
// return 0 on success, -1 for error
int pass_n_bytes(int infd, int outfd, int n);

int shift_file_contents(int fd, off_t start_idx, int amount);

// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(struct sockaddr_in *sockaddr, const char *ip_addr, int port, int domain, int type, int protocol, bool do_bind);

int init_sockaddr(int sockfd, struct sockaddr_in *sockaddr, const char *ip_addr, int port, int domain);

// assign opcode to first byte of pkt_buf
// return 0 on success, -1 on error
int assign_pkt_opcode(char *pkt_buf, int opcode);

// assign winsz to header bytes of pkt_buf
// return 0 on success, -1 on error
int assign_wr_winsz(char *pkt_buf, u_int32_t winsz);

// assign client_id to header bytes of pkt_buf
// return 0 on success, -1 on error
int assign_pkt_client_id(char *pkt_buf, u_int32_t client_id);

// assign ack sn to header bytes of pkt_buf
// return 0 on success, -1 on error
int assign_ack_sn(char *pkt_buf, u_int32_t sn);

// assign pkt_sn to header bytes of pkt_buf
// return 0 on success, -1 on error
int assign_pkt_sn(char *pkt_buf, u_int32_t pkt_sn);

// assign pyld_sz to header bytes of pkt_buf
// return 0 on success, -1 on error
int assign_pkt_pyld_sz(char *pkt_buf, u_int32_t pyld_sz);

// returns opcode of pkt_buf, -1 on error
int get_pkt_opcode(char *pkt_buf);

// returns window size of pkt_buf, 0 on error
u_int32_t get_write_req_winsz(char *pkt_buf);

// returns sn of pkt_buf, 0 on error
u_int32_t get_wr_sn(char *pkt_buf);

// returns client id of pkt_buf, 0 on error
u_int32_t get_data_client_id(char *pkt_buf);

// returns pkt sn of pkt_buf if data pkt, 0 on error and sets errno to 1
u_int32_t get_data_sn(char *pkt_buf);

// returns payload size of pkt_buf if data pkt, 0xffffffff on error
u_int32_t get_data_pyld_sz(char *pkt_buf);

// returns pkt sn of pkt_buf if ack pkt, 0 on error and sets errno to 1
// can be used to get client ID from server, server assigns pkt_sn field to client ID when accepting handshake
u_int32_t get_ack_sn(char *pkt_buf);

// create all directories in file path (if they don't exist)
// return 0 on success, -1 on error
int create_file_directory(const char *file_path);



#endif
