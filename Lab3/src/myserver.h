#ifndef MYSERVER_INCLUDE
#define MYSERVER_INCLUDE

// receive data from sockfd and echo it back as it arrives back to the client
// this function will run forever once called, or until there is an error (returns -1)
int echo_data(int sockfd, struct sockaddr *sockaddr, socklen_t *sock_size);

#endif
