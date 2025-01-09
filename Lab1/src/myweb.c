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
#include "myweb.h"

#define EMPTY_LINE_REGEX "\r\n"
#define DOUBLE_EMPTY_LINE_REGEX EMPTY_LINE_REGEX EMPTY_LINE_REGEX
#define CLH_REGEX "(Content-Length): ([ -~]{1,128})" EMPTY_LINE_REGEX
#define IP_REGEX "([[0-9]{1,3}.]{3}[0-9]{1,3})"
#define PORT_REGEX "[[:]{0,1}([0-9]{1,5})]{0,1}"
#define PATH_REGEX "(/[ -~]+)"
#define DOC_ARG_REGEX IP_REGEX PORT_REGEX PATH_REGEX

#define BUFFER_SIZE 8196

struct doc_data {
	char *ip_addr;
	int port;
	char *doc_path;
};

int main(int argc, char **argv) {
	// parse command line args

	int header_opt = argc < 4 ? 0 : !strcmp(argv[3], "-h");

	// wrong number of argc or extra option is not -h
	if (argc < 3 || argc > 4 || (argc == 4 && !header_opt)) {
		usage(argv[0]);
		exit(1);
	}

	char *url_arg = argv[1];
	char *doc_arg = argv[2];

	printf("url: %s\n", url_arg);
	printf("doc arg: %s\n", doc_arg);
	printf("header option?: %d\n", header_opt);

	struct doc_data data;

	parse_path(&data, doc_arg);

	free(data.ip_addr);
	free(data.doc_path);

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

// parse document path/ip address field into doc_data struct for further use
// returns 0 on success, -1 on error
int parse_path(struct doc_data *data, const char *path) {
	data->port = -1;

	// regex for parsing path arg
	regex_t regex_;
	// int res = regcomp(&regex_, DOC_ARG_REGEX, REG_EXTENDED);
	// if (res < 0) {
	// 	fprintf(stderr, "parse_path(): regcomp() failed\n");
	// 	return data;
	// }

	// regmatch_t pmatch[4];
	// res = regexec(&regex_, path, 4, pmatch, 0);
	// if (res < 0) {
	// 	if (res != REG_NOMATCH) {
	// 		fprintf(stderr, "parse_path(): regexec() failed\n");
	// 		return data;
	// 	}
	// 	fprintf(stderr, "parse_path(): no regex match found for doc path arg. Invalid IP/Port/document path given.\n");
	// 	return data;
	// }

	// data.ip_addr = calloc(pmatch[1].rm_eo - pmatch[1].rm_so, sizeof(char));
	// memcpy(data.ip_addr, path + pmatch[1].rm_so, sizeof(data.ip_addr));

	// char ip_buf[pmatch[2].rm_eo - pmatch[2].rm_so + 1];
	// ip_buf[sizeof(ip_buf) - 1] = 0;
	// memcpy(ip_buf, path + pmatch[2].rm_so, sizeof(ip_buf));

	// data.port = atoi(ip_buf);

	// regex for separating ip/port and doc path
	int res = regcomp(&regex_, "/", 0);
	if (res < 0) {
		fprintf(stderr, "parse_path(): regcomp() failed\n");
		return -1;
	}

	regmatch_t pmatch[2];

	res = regexec(&regex_, path, 1, &pmatch[1], 0);
	if (res < 0 && res != REG_NOMATCH) {
		fprintf(stderr, "parse_path(): regexec() failed\n");
		return -1;
	}

	// allocate and transfer data from path string to ip address field
	int doc_path_len = strlen(path) - pmatch[1].rm_so;
	data->doc_path = calloc(doc_path_len + 1, sizeof(char));
	data->doc_path[doc_path_len] = 0;
	memcpy(data->doc_path, path + pmatch[1].rm_so, doc_path_len);
	

	// regex for separating ip and port
	res = regcomp(&regex_, ":", 0);
	if (res < 0) {
		fprintf(stderr, "parse_path(): regcomp() failed\n");
		return -1;
	}

	res = regexec(&regex_, path, 1, &pmatch[0], 0);
	if (res < 0 && res != REG_NOMATCH) {
		fprintf(stderr, "parse_path(): regexec() failed\n");
		return -1;
	}

	int ip_addr_len;
	if (res == REG_NOMATCH) {
		ip_addr_len = pmatch[1].rm_so;
	} else {
		ip_addr_len = pmatch[0].rm_eo - 1;

		int port_buf_len = pmatch[1].rm_so - pmatch[0].rm_eo;
		char *port_buf = calloc(port_buf_len + 1, sizeof(char));
		memcpy(port_buf, path + pmatch[0].rm_eo, port_buf_len);
		data->port = atoi(port_buf);
		if (data->port <= 0) {
			fprintf(stderr, "parse_path(): invalid port specified\n");
			return -1;
		}
		free(port_buf);
	}

	// int ip_addr_len = pmatch[1].rm_eo - pmatch[1].rm_so;
	data->ip_addr = calloc(ip_addr_len + 1, sizeof(char));
	data->ip_addr[ip_addr_len] = 0;
	memcpy(data->ip_addr, path, ip_addr_len);

	printf("IP: %s\n", data->ip_addr);
	printf("Port: %d\n", data->port);
	printf("Doc path: %s\n", data->doc_path);

	return 0;
}

// prints correct program usage to stderr
void usage(char *exec) {
    fprintf(stderr,
        "SYNOPSIS\n"
        "Makes a simple GET request and download for a file from a specified url and IPv4 address.\n"
		"/Outputs requested file contents to output.dat\n"
        "\n"
        "USAGE\n"
        "   %s [url] [IPv4]:[optional receiving port]/[desired file name] [-h]\n"
        "\n"
        "OPTIONS\n"
        "   -h               prints server response headers to stdout, and does not receive document\n",
        exec);
}
