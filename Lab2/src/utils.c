#include "stdio.h"
#include <stdlib.h>
#include <unistd.h>

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

	printf("split: %u\n", val & 0x0000ff00);
	printf("split2: %u\n", res[2]);

	return res;
}

uint32_t reunite_bytes(uint8_t *bytes) {
	uint32_t res;
	res = (uint32_t) ((bytes[0] << 24) & 0xff000000) | (uint32_t) ((bytes[1] << 16) & 0x00ff0000) | (uint32_t) ((bytes[2] << 8) & 0x0000ff00) | (uint32_t) (bytes[3] & 0x000000ff);
	return res; 
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
