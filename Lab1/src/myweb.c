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
	printf("%d bytes sent\n", res);

	char buf[BUFFER_SIZE] = { 0 };
	int off = 0;

	// int bytes_read = read(sockfd, buf, sizeof(buf) - 1); // initial read into buffer for parsing header fields

	// int bytes_read = 0;

	// while ((bytes_read = read(sockfd, buf + off, sizeof(buf) - off - 1)) > 0) {
	// 	off += bytes_read;
	// }

	int bytes_read = read_until(sockfd, buf, DOUBLE_EMPTY_LINE_REGEX, 4, sizeof(buf));

	if (bytes_read < 0) {
		fprintf(stderr, "initial read failed\n");
		exit(1);
	}

	fprintf(stderr, "%d bytes read\n", off);

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
		exit(1);
	}

	char cl_str[pmatch[2].rm_eo - pmatch[2].rm_so];
	memcpy(cl_str, buf + pmatch[2].rm_so, sizeof(cl_str));
	int content_length = strtoll(cl_str, NULL, 10);

	fprintf(stderr, "\n\nCONTENT LENGTH: %d\n\n", content_length);

	// regex for finding start of content
	res = regcomp(&regex_, DOUBLE_EMPTY_LINE_REGEX, 0);
	if (res != 0) {
		fprintf(stderr, "regcomp 2 failed\n");
		exit(1);
	}

	res = regexec(&regex_, buf, 1, pmatch, 0);

	if (res != 0) {
		if (res == REG_NOMATCH) {
			fprintf(stderr, "no match for start of content, something probably went wrong\n");
			exit(1);
		} else {
			fprintf(stderr, "regexec 2 failed\n");
			fprintf(stderr, "%d\n", res);
			exit(1);
		}
	}
	
	// int content_bytes_read = bytes_read - pmatch[0].rm_eo;

	// while (content_bytes_read < content_length && (res = read(sockfd, buf, sizeof(buf) - 1 > content_length - content_bytes_read ? content_length - content_bytes_read : sizeof(buf) - 1)) > 0) {
	// 	printf("%s", buf);
	// 	fprintf(stderr, "%d bytes read\n", res);
	// 	content_bytes_read += res;
	// }

	printf("\n");

	if (res < 0) {
		fprintf(stderr, "response failed\n");
		exit(1);
	}

	close(sockfd);

	return 0;
}
