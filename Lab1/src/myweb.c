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

// prints correct program usage to stderr
void usage(char *exec) {
    fprintf(stderr,
        "SYNOPSIS\n"
        "Makes a simple GET request and download for a file from a specified url and IPv4 address.\n"
		"Outputs requested file contents to output.dat\n"
        "\n"
        "USAGE\n"
        "   %s [url] [IPv4]:[optional receiving port]/[desired file name] [-h]\n"
        "\n"
        "OPTIONS\n"
        "   -h               prints server response headers to stdout, and does not receive document\n",
        exec);
}

// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(const char *ip_addr, int port) {
	// initialize socket fd
	int sockfd = socket(PF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		fprintf(stderr, "init_socket(): failed to initialize socket\n");
		return -1;
	}

	// init socket address struct with ip and port
	struct sockaddr_in sockaddr;

	sockaddr.sin_family = AF_INET; // IPv4
	sockaddr.sin_port = htons(port); // convert port endianness
	if (inet_pton(AF_INET, ip_addr, &sockaddr.sin_addr.s_addr) < 0) {
		fprintf(stderr, "init_socket(): invalid ip address provided\n");
		return -1;
	}

	// connect socket
	if (connect(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0) {
		fprintf(stderr, "init_socket(): failed to connect socket\n");
		return -1;
	}

	return sockfd;
}

int main(int argc, char **argv) {
	// parse command line args

	for (int i = 1; i < argc; i++) {
		printf("%s\n", argv[i]);
	}

	return 0;

	char *ip_addr;
	int port;
	
	int sockfd = init_socket(ip_addr, port);
	if (sockfd < 0) {
		fprintf(stderr, "main(): failed to initialize socket with IP %s:%d\n", ip_addr, port);
		exit(1);
	}

	char *req = "GET /index.html HTTP/1.1\r\nHost: www.example.com\r\n\r\n";

	int res = send(sockfd, req, strlen(req), 0);
	// printf("%d bytes sent\n", res);

	char buf[BUFFER_SIZE] = { 0 };

	int bytes_read = read_until(sockfd, buf, sizeof(buf), DOUBLE_EMPTY_LINE_REGEX);

	if (bytes_read < 0) {
		fprintf(stderr, "initial read failed\n");
		exit(1);
	}

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
