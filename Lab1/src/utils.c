#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>

// read into buf from sockfd. reads n bytes or the size of buf, whichever is smaller
// return 0 on success, -1 for error
int read_n_bytes(int sockfd, const char *buf, int n) {
	int offset = 0;
	int bytes_read = 0;

	while ((bytes_read = read(sockfd, buf + offset, n)) > 0) {
		offset += bytes_read;
	}

	if (bytes_read < 0) {
		fprintf(stderr, "read_n_bytes(): read() failed\n");
		return -1;
	}

	return offset;
}

// read from sockfd into buf until match regular expression is found or max_chars characters have been read
// return number of bytes read, or -1 for error
int read_until(int sockfd, const char *buf, const char *match, int match_len, int max_chars) {
	regex_t regex_;
	int res = regcomp(&regex_, match, 0);
	if (res < 0) {
		fprintf(stderr, "read_until(): regcomp() failed\n");
		return -1;
	}

	memset(buf, 0, sizeof(buf));

	regmatch_t *pmatch;

	int bytes_read = 0;
	int offset = 0;
	while (offset < max_chars && (bytes_read = read_n_bytes(sockfd, buf + offset, match_len)) > 0) {
		offset += 1;

		res = regexec(&regex_, buf, 1, pmatch, 0);
		if (res < 0 && res != REG_NOMATCH) {
			fprintf(stderr, "read_until(): regexec() failed\n");
			return -1;
		} else if (res == 0) {
			return pmatch->rm_eo;
		}
	}

}
