#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <regex.h>
#include "utils.h"

#define EMPTY_LINE_REGEX "\r\n"
#define DOUBLE_EMPTY_LINE_REGEX EMPTY_LINE_REGEX EMPTY_LINE_REGEX
#define CLH_REGEX "(Content-Length): ([ -~]{1,128})" EMPTY_LINE_REGEX
#define BUFFER_SIZE 8196

int main(void) {
	// initialize socket fd
	int sockfd = socket(PF_INET, SOCK_STREAM, 0);

	// init socket address struct with ip and port
	struct sockaddr_in sockaddr;

	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(80);
	sockaddr.sin_addr.s_addr = inet_addr("93.184.216.34");

	if (sockaddr.sin_addr.s_addr == INADDR_NONE) {
		fprintf(stderr, "invalid ip address\n");

		exit(1);
	}

	// connect socket
	int res = connect(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));

	if (res < 0) {
		fprintf(stderr, "connection failed\n");
		exit(1);
	}

	char *req = "GET /index.html HTTP/1.1\r\nHost: www.example.com\r\n\r\n";

	res = send(sockfd, req, strlen(req), 0);
	// printf("%d bytes sent\n", res);

	char buf[BUFFER_SIZE] = { 0 };

	int bytes_read = read_until(sockfd, buf, sizeof(buf), DOUBLE_EMPTY_LINE_REGEX);

	if (bytes_read < 0) {
		fprintf(stderr, "initial read failed\n");
		exit(1);
	}

	// fprintf(stderr, "%d bytes read\n", bytes_read);

	// regex for matching content-length header field
	regex_t regex_;
	res = regcomp(&regex_, CLH_REGEX, REG_EXTENDED);
	if (res != 0) {
		fprintf(stderr, "regcomp failed\n");
		exit(1);
	}

	regmatch_t pmatch[3];

	res = regexec(&regex_, buf, 3, pmatch, 0);

	if (res != 0) {
		fprintf(stderr, "regexec failed\n");
		fprintf(stderr, "res == REG_NOMATCH: %s\n", res == REG_NOMATCH ? "true" : "false");
		fprintf(stderr, "\nheader: %s\n", buf);
		exit(1);
	}

	char cl_str[pmatch[2].rm_eo - pmatch[2].rm_so];
	memcpy(cl_str, buf + pmatch[2].rm_so, sizeof(cl_str));
	int content_length = strtoll(cl_str, NULL, 10);

	// fprintf(stderr, "\n\nCONTENT LENGTH: %d\n\n", content_length);

	pass_n_bytes(sockfd, STDOUT_FILENO, content_length);

	close(sockfd);

	return 0;
}
