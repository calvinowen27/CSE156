#ifndef UTILS_INCLUDE
#define UTILS_INCLUDE

#include <stdbool.h>

struct sockaddr_in;

void logerr(const char *);

// split uint32_t into uint8_t[4]
uint8_t *split_bytes(uint32_t val);

// reuinite uint8_t[4] into uin32_t
uint32_t reunite_bytes(uint8_t *bytes);

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

#endif
