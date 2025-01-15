#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>

#define BUFFER_SIZE 4096

// read into buf from sockfd. reads n bytes or the size of buf, whichever is smaller
// return bytes read on success, -1 for error
int read_n_bytes(int sockfd, char *buf, int n) {
	int offset = 0;
	int bytes_read = 0;

	while (offset < n && (bytes_read = read(sockfd, buf + offset, n - offset)) > 0) {
		offset += bytes_read;
	}

	if (bytes_read < 0) {
		fprintf(stderr, "read_n_bytes(): read() failed\n");
		return -1;
	}

	return offset;
}

// write n bytes from buf into sockfd
// returns bytes written on success, -1 on error
int write_n_bytes(int sockfd, char *buf, int n) {
	int bytes_written = 0;
	int offset = 0;

	while (offset < n && (bytes_written = write(sockfd, buf, n - offset)) > 0) {
		offset += bytes_written;
	}

	if (bytes_written < 0) {
		fprintf(stderr, "write_n_bytes(): write() failed\n");
		return -1;
	}

	if (offset != n) {
		fprintf(stderr, "write_n_bytes(): bytes written does not equal n\n");
		return -1;
	}

	return offset;
}

// read from sockfd into buf until match regular expression is found or max_chars characters have been read
// return number of bytes read, or -1 for error
int read_until(int sockfd, char *buf, int buf_len, const char *match) {
	regex_t regex_;
	int res = regcomp(&regex_, match, 0);
	if (res < 0) {
		fprintf(stderr, "read_until(): regcomp() failed\n");
		return -1;
	}

	memset(buf, 0, buf_len);

	regmatch_t pmatch;

	int offset = 0;
	while (offset < buf_len && read(sockfd, buf + offset, 1) > 0) {
		offset += 1;

		res = regexec(&regex_, buf, 1, &pmatch, 0);
		if (res < 0 && res != REG_NOMATCH) {
			fprintf(stderr, "read_until(): regexec() failed\n");
			return -1;
		} else if (res == 0) {
			return pmatch.rm_eo;
		}
	}

	return offset;
}

// continually read bytes from infd and write them to outfd, until n bytes have been passed
// return 0 on success, -1 for error
int pass_n_bytes(int infd, int outfd, int n) {
	char buf[n];
	
	int bytes_read = 0;
	int total_bytes_read = 0;
	while ((bytes_read = read_n_bytes(infd, buf, n)) < n) {
		if (bytes_read < 0) {
			fprintf(stderr, "pass_n_bytes(): read_n_bytes() failed\n");
			return -1;
		}

		if (write_n_bytes(outfd, buf, bytes_read) < 0) {
			fprintf(stderr, "pass_n_bytes(): write_n_bytes() failed\n");
			return -1;
		}

		total_bytes_read += bytes_read;
	}

	if (write_n_bytes(outfd, buf, bytes_read) < 0) {
		fprintf(stderr, "pass_n_bytes(): write_n_bytes() failed\n");
		return -1;
	}

	total_bytes_read += bytes_read;

	if (total_bytes_read != n) {
		fprintf(stderr, "pass_n_bytes(): bytes read does not equal n\n");
		return -1;
	}

	return 0;
}

// continually read bytes from infd and write them to outfd, until EOF is reached
// return 0 on success, -1 for error
int pass_file(int infd, int outfd) {
	char buf[BUFFER_SIZE];

	int bytes_read = 0;
	while ((bytes_read = read(infd, buf, sizeof(buf))) > 0) {
		if (write_n_bytes(outfd, buf, bytes_read) < 0) {
			fprintf(stderr, "pass_n_bytes(): write_n_bytes() failed\n");
			return -1;
		}
	}

	if (bytes_read < 0) {
		fprintf(stderr, "pass_n_bytes(): read_n_bytes() failed\n");
		return -1;
	}

	return 0;
}
