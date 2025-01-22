// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(struct sockaddr_in *sockaddr, const char *ip_addr, int port);

// send file from fd to sockfd, also using sockaddr
// return 0 on success, -1 on error
int send_recv_file(int infd, int outfd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mtu);
