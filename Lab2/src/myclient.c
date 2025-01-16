#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "myclient.h"
#include "utils.h"

int main(void) {
	struct sockaddr_in serveraddr;
	int serveraddr_size = sizeof(serveraddr);

	int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0) {
		logerr("main(): client failed to initialize socket");
	}

	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(1234);
	serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (sendto(sockfd, "1hello1", 8, 0, (struct sockaddr *)&serveraddr, serveraddr_size) < 0) {
		logerr("main(): client failed to send message to server");
	}

	printf("client done\n");

	close(sockfd);

	return 0;
}
