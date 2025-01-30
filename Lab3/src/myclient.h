#ifndef MYCLIENT_INCLUDE
#define MYCLIENT_INCLUDE

// send file from fd to sockfd, also using sockaddr
// return 0 on success, -1 on error
int send_recv_file(int infd, int outfd, int sockfd, struct sockaddr *sockaddr, socklen_t sockaddr_size, int mtu);

// create all directories in file path (if they don't exist)
// return 0 on success, -1 on error
int create_file_directory(const char *file_path);

#endif
