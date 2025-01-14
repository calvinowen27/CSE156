#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <regex.h>
#include <fcntl.h>
#include "utils.h"
#include "myweb.h"

#define EMPTY_LINE_REGEX "\r\n"
#define DOUBLE_EMPTY_LINE_REGEX EMPTY_LINE_REGEX EMPTY_LINE_REGEX
#define CLH_REGEX "(Content-Length): ([ -~]{1,128})" EMPTY_LINE_REGEX
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

	struct doc_data data;
	if (parse_path(&data, doc_arg) < 0) {
		fprintf(stderr, "main(): failed to parse IP and document path: %s\n", doc_arg);
		exit(1);
	}
	
	// initialize socket with user data
	int sockfd = init_socket(data.ip_addr, data.port);
	if (sockfd < 0) {
		fprintf(stderr, "main(): failed to initialize socket with IP %s:%d\n", data.ip_addr, data.port);
		exit(1);
	}

	// form request string
	int req_len = 4 + strlen(data.doc_path) + 17 + strlen(url_arg) + 4;
	char req[req_len + 1];
	req[req_len] = 0;
	sprintf(req, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", data.doc_path, url_arg);

	if (send(sockfd, req, strlen(req), 0) < 0) {
		fprintf(stderr, "main(): failed to send request\n");
		exit(1);
	}

	char buf[BUFFER_SIZE + 1] = { 0 };

	int bytes_read = read_until(sockfd, buf, sizeof(buf), DOUBLE_EMPTY_LINE_REGEX);

	if (bytes_read < 0) {
		fprintf(stderr, "main(): initial response read failed\n");
		exit(1);
	}

	int content_length = get_content_length(buf);

	if (header_opt) {
		printf("%s\n", buf);
	} else {
		int outfd = creat("output.dat", 0666);
		if (content_length > 0) {
			pass_n_bytes(sockfd, outfd, content_length);
		} else {
			pass_file(sockfd, outfd);
		}
		close(outfd);
	}

	close(sockfd);

	free(data.ip_addr);
	free(data.doc_path);

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

	// printf("\n\nIP: %d\n\n", sockaddr.sin_addr.s_addr);

	// connect socket
	if (connect(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0) {
		fprintf(stderr, "init_socket(): failed to connect socket\n");
		return -1;
	}

	return sockfd;
}

// parse header buffer and get value of Content-Length header field
int get_content_length(char *buf) {
	// regex for matching content-length header field
	regex_t regex_;
	if (regcomp(&regex_, CLH_REGEX, REG_EXTENDED) != 0) {
		fprintf(stderr, "get_content_length(): regcomp failed for Content-Length header\n");
		return -1;
	}

	regmatch_t pmatch[3];

	if (regexec(&regex_, buf, 3, pmatch, 0) != 0) {
		fprintf(stderr, "get_content_length(): regexec failed for Content-Length header\n");
		return -1;
	}

	int cl_len = pmatch[2].rm_eo - pmatch[2].rm_so;
	char cl_buf[cl_len + 1];
	cl_buf[cl_len] = 0;
	memcpy(cl_buf, buf + pmatch[2].rm_so, sizeof(cl_buf));
	return atoi(cl_buf);
}

// parse document path/ip address field into doc_data struct for further use
// returns 0 on success, -1 on error
int parse_path(struct doc_data *data, const char *path) {
	data->port = 80;

	regex_t regex_;

	// regex for separating ip/port and doc path
	int res = regcomp(&regex_, "/", 0);
	if (res < 0) {
		fprintf(stderr, "parse_path(): regcomp() failed for '/'\n");
		return -1;
	}

	regmatch_t pmatch[2];

	res = regexec(&regex_, path, 1, &pmatch[1], 0);
	if (res != 0) {
		fprintf(stderr, "parse_path(): regexec() failed for '/', likely invalid IP and document path provided\n");
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
		fprintf(stderr, "parse_path(): regcomp() failed for ':'\n");
		return -1;
	}

	res = regexec(&regex_, path, 1, &pmatch[0], 0);
	if (res < 0 && res != REG_NOMATCH) {
		fprintf(stderr, "parse_path(): regexec() failed for ':'\n");
		return -1;
	}

	int ip_addr_len;
	if (res == REG_NOMATCH) {
		ip_addr_len = pmatch[1].rm_so;
	} else {
		ip_addr_len = pmatch[0].rm_eo - 1;

		// match on ':' so separate port from ip address and convert to int
		int port_buf_len = pmatch[1].rm_so - pmatch[0].rm_eo;
		char port_buf[port_buf_len + 1];
		memcpy(port_buf, path + pmatch[0].rm_eo, port_buf_len);
		data->port = atoi(port_buf);
		if (data->port <= 0 || data->port > 65535) {
			fprintf(stderr, "parse_path(): invalid port specified. Port must be integer between 0 and 65535.\n");
			return -1;
		}
	}

	data->ip_addr = calloc(ip_addr_len + 1, sizeof(char));
	data->ip_addr[ip_addr_len] = 0;
	memcpy(data->ip_addr, path, ip_addr_len);

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
