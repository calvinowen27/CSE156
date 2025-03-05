#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <regex.h>
#include <poll.h>

#include "utils.h"

void logerr(const char *err) {
	fprintf(stderr, "%s\n", err);
}

// split uint32_t into uint8_t[4]
// must free pointer when done using it
u_int8_t *split_bytes(u_int32_t val) {
	u_int8_t *res = calloc(4, sizeof(u_int8_t *));

	res[0] = (u_int8_t) ((val & 0xff000000) >> 24);
	res[1] = (u_int8_t) ((val & 0x00ff0000) >> 16);
	res[2] = (u_int8_t) ((val & 0x0000ff00) >> 8);
	res[3] = (u_int8_t) (val & 0x000000ff);

	return res;
}

// reuinite uint8_t[4] into uin32_t
u_int32_t reunite_bytes(u_int8_t *bytes) {
	u_int32_t res;
	res = (u_int32_t) ((bytes[0] << 24) & 0xff000000) | (u_int32_t) ((bytes[1] << 16) & 0x00ff0000) | (u_int32_t) ((bytes[2] << 8) & 0x0000ff00) | (u_int32_t) (bytes[3] & 0x000000ff);
	return res; 
}

// read into buf from sockfd. reads n bytes or the size of buf, whichever is smaller
// return bytes read on success, -1 for error
int read_n_bytes(int fd, char *buf, int n) {
	int offset = 0;
	int bytes_read = 0;

	struct pollfd fds[1] = { {fd, POLLIN, 0 } };

	int poll_res;
	while (offset < n && (poll_res = poll(fds, 1, 0)) > 0 && (bytes_read = read(fd, buf + offset, n - offset)) > 0) {
		offset += bytes_read;
	}

	if (bytes_read < 0) {
		fprintf(stderr, "read_n_bytes(): read() failed\n");
		fprintf(stderr, "%s\n", strerror(errno));
		return -1;
	}

	if (poll_res < 0) {
		fprintf(stderr, "read_n_bytes(): poll() failed\n");
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

int append_buf(char **buf, size_t buf_size, char *addtl, size_t addtl_size) {
	if (strlen(*buf) + addtl_size > buf_size) {
		(*buf) = realloc(*buf, strlen(*buf) + addtl_size);
		if (*buf == NULL) {
			fprintf(stderr, "append_buf(): failed to reallocate buffer\n");
			return -1;
		}
	}

	memset((*buf) + strlen(*buf), 0, buf_size - strlen(*buf));

	memcpy((*buf) + strlen(*buf), addtl, addtl_size);

	return 0;
}

int shift_file_contents(int fd, off_t start_idx, int amount) {
	off_t prev_seek = lseek(fd, 0, SEEK_CUR);
	
	off_t begin = lseek(fd, 0, SEEK_END);

	char buf[amount];

	while (begin > start_idx) {
		begin = lseek(fd, begin - amount, SEEK_SET);

		if (read_n_bytes(fd, buf, amount) < 0) {
			fprintf(stderr, "shift_file_contents(): read_n_bytes() failed.\n");
			return -1;
		}

		if (write_n_bytes(fd, buf, amount) < 0) {
			fprintf(stderr, "shift_file_contents(): write_n_bytes failed.\n");
			return -1;
		}
	}

	lseek(fd, prev_seek, SEEK_SET);

	return 0;
}

// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(struct sockaddr_in *sockaddr, const char *ip_addr, int port, int domain, int type, int protocol, bool do_bind) {
	// initialize socket fd
	int sockfd = socket(domain, type, protocol);
	if (sockfd < 0) {
		fprintf(stderr, "utils ~ init_socket(): failed to initialize socket\n");
		return -1;
	}

	if (init_sockaddr(do_bind ? sockfd : -1, sockaddr, ip_addr, port, domain) < 0) {
		fprintf(stderr, "utils ~ init_socket(): failed to initialize sockaddr: %s\n", strerror(errno));
		return -1;
	}

	return sockfd;
}

int init_sockaddr(int sockfd, struct sockaddr_in *sockaddr, const char *ip_addr, int port, int domain) {
	// init socket address struct with ip and port
	sockaddr->sin_family = domain; // IPv4
	sockaddr->sin_port = htons(port); // convert port endianness

	if (ip_addr == NULL) {
		sockaddr->sin_addr.s_addr = INADDR_ANY;
	} else if (inet_pton(domain, ip_addr, &(sockaddr->sin_addr.s_addr)) < 0) {
		fprintf(stderr, "utils ~ init_sockaddr(): invalid ip address provided\n");
		return -1;
	}

	// bind socket
	if (sockfd > 0 && bind(sockfd, (struct sockaddr *)sockaddr, sizeof(*sockaddr)) < 0) {
		fprintf(stderr, "utils ~ init_sockaddr(): failed to bind socket: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

// create all directories in file path (if they don't exist)
// return 0 on success, -1 on error
int create_file_directory(const char *file_path) {
	// regex for matching last / in filepath
	regex_t regex_;
	if (regcomp(&regex_, "/", 0) < 0) {
		fprintf(stderr, "utils ~ create_file_directory(): regcomp() failed\n");
		return -1;
	}

	regmatch_t pmatch;
	int res = 0, fs_idx = 0;

	while (res != REG_NOMATCH) {
		if ((res = regexec(&regex_, file_path + fs_idx, 1, &pmatch, 0)) < 0) {
			fprintf(stderr, "utils ~ create_file_directory(): regexec() failed\n");
			return -1;
		} else if (res != REG_NOMATCH) {
			fs_idx += pmatch.rm_eo;

			char outfile_dir[fs_idx + 1];
			outfile_dir[fs_idx] = 0;
			memcpy(outfile_dir, file_path, fs_idx );

			struct stat st = {0};
			if (stat(outfile_dir, &st) == -1) {
				if (mkdir(outfile_dir, 0700) < 0 && errno != EEXIST) {
					fprintf(stderr, "utils ~ create_file_directory(): failed to make directory %s\n", outfile_dir);
					fprintf(stderr, "\t%s\n", strerror(errno));
					return -1;
				}
			}
		}
	}
	
	regfree(&regex_);

	return 0;
}

bool sockaddrs_eq(struct sockaddr sockaddr1, struct sockaddr sockaddr2) {
	if (sockaddr1.sa_family != sockaddr2.sa_family) {
		return false;
	}

	// if (sockaddr1.sa_len != sockaddr2.sa_len) {
	// 	return false;
	// }

	for (unsigned long i = 0; i < sizeof(sockaddr1.sa_data); i++) {
		if (sockaddr1.sa_data[i] != sockaddr2.sa_data[i]) {
			return false;
		}
	}

	return true;
}
