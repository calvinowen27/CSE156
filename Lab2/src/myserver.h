// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(const char *ip_addr, int port, struct sockaddr_in *sockaddr);
