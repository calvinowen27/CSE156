#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include "utils.h"

void logerr(const char *err) {
	fprintf(stderr, "%s\n", err);
}

// split uint32_t into uint8_t[4]
uint8_t *split_bytes(uint32_t val) {
	uint8_t *res = calloc(4, sizeof(uint8_t *));

	res[0] = (uint8_t) ((val & 0xff000000) >> 24);
	res[1] = (uint8_t) ((val & 0x00ff0000) >> 16);
	res[2] = (uint8_t) ((val & 0x0000ff00) >> 8);
	res[3] = (uint8_t) (val & 0x000000ff);

	return res;
}

// reuinite uint8_t[4] into uin32_t
uint32_t reunite_bytes(uint8_t *bytes) {
	uint32_t res;
	res = (uint32_t) ((bytes[0] << 24) & 0xff000000) | (uint32_t) ((bytes[1] << 16) & 0x00ff0000) | (uint32_t) ((bytes[2] << 8) & 0x0000ff00) | (uint32_t) (bytes[3] & 0x000000ff);
	return res; 
}

// read into buf from sockfd. reads n bytes or the size of buf, whichever is smaller
// return bytes read on success, -1 for error
int read_n_bytes(int fd, char *buf, int n) {
	int offset = 0;
	int bytes_read = 0;

	while (offset < n && (bytes_read = read(fd, buf + offset, n - offset)) > 0) {
		offset += bytes_read;
	}

	if (bytes_read < 0) {
		fprintf(stderr, "read_n_bytes(): read() failed\n");
		fprintf(stderr, "%s\n", strerror(errno));
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

int shift_file_contents(int fd, off_t start_idx, int amount) {
	off_t prev_seek = lseek(fd, 0, SEEK_CUR);
	
	off_t begin = lseek(fd, 0, SEEK_END);

	char buf[amount];

	while (begin > start_idx) {
		begin = lseek(fd, begin - amount, SEEK_SET);
		printf(" = %lld\n", begin);

		if (read_n_bytes(fd, buf, amount) < 0) {
			fprintf(stderr, "shift_file_contents(): read_n_bytes() failed.\n");
			return -1;
		}
		printf("writing '%s' at index %lld\n", buf, begin);
		if (write_n_bytes(fd, buf, amount) < 0) {
			fprintf(stderr, "shift_file_contents(): write_n_bytes failed.\n");
			return -1;
		}
	}

	lseek(fd, prev_seek, SEEK_SET);

	return 0;
}
