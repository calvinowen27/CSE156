// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(struct sockaddr_in *sockaddr, const char *ip_addr, int port);

// receive data from sockfd and echo it back as it arrives back to the client
// this function will run forver once called, or until there is an error (returns -1)
int echo_data(int sockfd, struct sockaddr *sockaddr, socklen_t *sock_size);
