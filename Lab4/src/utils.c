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

#include "utils.h"
#include "protocol.h"

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

	// init socket address struct with ip and port
	sockaddr->sin_family = domain; // IPv4
	sockaddr->sin_port = htons(port); // convert port endianness

	if (ip_addr == NULL) {
		sockaddr->sin_addr.s_addr = INADDR_ANY;
	} else if (inet_pton(domain, ip_addr, &(sockaddr->sin_addr.s_addr)) < 0) {
		fprintf(stderr, "utils ~ init_socket(): invalid ip address provided\n");
		return -1;
	}

	// bind socket
	if (do_bind && bind(sockfd, (struct sockaddr *)sockaddr, sizeof(*sockaddr)) < 0) {
		fprintf(stderr, "utils ~ init_socket(): failed to bind socket: %s\n", strerror(errno));
		return -1;
	}

	return sockfd;
}

// assign opcode to first byte of pkt_buf
// return 0 on success, -1 on error
int assign_pkt_opcode(char *pkt_buf, int opcode) {
	pkt_buf[0] = (u_int8_t)opcode;
	return 0;
}

// assign winsz to header bytes of pkt_buf
// return 0 on success, -1 on error
int assign_wr_winsz(char *pkt_buf, u_int32_t winsz) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ assign_wr_winsz(): cannot pass NULL ptr to pkt_buf.\n");
		return -1;
	}

	u_int8_t *bytes = split_bytes(winsz);

	if (bytes == NULL) {
		fprintf(stderr, "utils ~ assign_wr_winsz(): something went wrong when splitting bytes of client_id.\n");
		return -1;
	}
	
	// client_id goes right after opcode (1)
	pkt_buf[1] = bytes[0];
	pkt_buf[2] = bytes[1];
	pkt_buf[3] = bytes[2];
	pkt_buf[4] = bytes[3];

	free(bytes);

	return 0;
}

// assign client_id to header bytes of pkt_buf
// return 0 on success, -1 on error
int assign_pkt_client_id(char *pkt_buf, u_int32_t client_id) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ assign_pkt_client_id(): cannot pass NULL ptr to pkt_buf.\n");
		return -1;
	}

	u_int8_t *bytes = split_bytes(client_id);

	if (bytes == NULL) {
		fprintf(stderr, "utils ~ assign_pkt_client_id(): something went wrong when splitting bytes of client_id.\n");
		return -1;
	}
	
	// client_id goes right after opcode (1)
	pkt_buf[1] = bytes[0];
	pkt_buf[2] = bytes[1];
	pkt_buf[3] = bytes[2];
	pkt_buf[4] = bytes[3];

	free(bytes);

	return 0;
}

// assign ack sn to header bytes of pkt_buf
// return 0 on success, -1 on error
int assign_ack_sn(char *pkt_buf, u_int32_t sn) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ assign_ack_sn(): cannot pass NULL ptr to pkt_buf.\n");
		return -1;
	}

	u_int8_t *bytes = split_bytes(sn);

	if (bytes == NULL) {
		fprintf(stderr, "utils ~ assign_ack_sn(): something went wrong when splitting bytes of client_id.\n");
		return -1;
	}
	
	// client_id goes right after opcode (1)
	pkt_buf[1] = bytes[0];
	pkt_buf[2] = bytes[1];
	pkt_buf[3] = bytes[2];
	pkt_buf[4] = bytes[3];

	free(bytes);

	return 0;
}

// assign pkt_sn to header bytes of pkt_buf
// return 0 on success, -1 on error
int assign_pkt_sn(char *pkt_buf, u_int32_t pkt_sn) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ assign_pkt_sn(): cannot pass NULL ptr to pkt_buf.\n");
		return -1;
	}

	u_int8_t *bytes = split_bytes(pkt_sn);

	if (bytes == NULL) {
		fprintf(stderr, "utils ~ assign_pkt_sn(): something went wrong when splitting bytes of pkt_sn.\n");
		return -1;
	}
	
	// pkt_sn goes right after client id (5)
	pkt_buf[5] = bytes[0];
	pkt_buf[6] = bytes[1];
	pkt_buf[7] = bytes[2];
	pkt_buf[8] = bytes[3];

	free(bytes);

	return 0;
}

// assign pyld_sz to header bytes of pkt_buf
// return 0 on success, -1 on error
int assign_pkt_pyld_sz(char *pkt_buf, u_int32_t pyld_sz) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ assign_pkt_pyld_sz(): cannot pass NULL ptr to pkt_buf.\n");
		return -1;
	}

	u_int8_t *bytes = split_bytes(pyld_sz);

	if (bytes == NULL) {
		fprintf(stderr, "utils ~ assign_pkt_pyld_sz(): something went wrong when splitting bytes of pyld_sz.\n");
		return -1;
	}
	
	// pyld_sz goes right after pkt sn (9)
	pkt_buf[9] = bytes[0];
	pkt_buf[10] = bytes[1];
	pkt_buf[11] = bytes[2];
	pkt_buf[12] = bytes[3];

	free(bytes);

	return 0;
}

// returns opcode of pkt_buf, -1 on error
int get_pkt_opcode(char *pkt_buf) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ get_pkt_opcode(): cannot pass NULL ptr to pkt_buf.\n");
		return -1;
	}

	return (int)pkt_buf[0];
}

// returns window size of pkt_buf, 0 on error
u_int32_t get_write_req_winsz(char *pkt_buf) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ get_write_req_window_sz(): cannot pass NULL ptr to pkt_buf.\n");
		return 0;
	}

	// pkt_sn occurs right after opcode for ack, not at all for error
	// check opcode for ack, otherwise return 0
	if ((int)pkt_buf[0] == OP_WR) {
		u_int8_t bytes[4];
		bytes[0] = pkt_buf[1];
		bytes[1] = pkt_buf[2];
		bytes[2] = pkt_buf[3];
		bytes[3] = pkt_buf[4];
		
		return reunite_bytes(bytes);
	} else {
		fprintf(stderr, "utils ~ get_write_req_window_sz(): pkt_buf does not contain valid opcode to get a sequence number.\n");
		return 0;
	}
}

// returns client id of pkt_buf, 0 on error
u_int32_t get_data_client_id(char *pkt_buf) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ get_data_client_id(): cannot pass NULL ptr to pkt_buf.\n");
		return 0;
	}

	// pkt_sn occurs right after opcode for ack, not at all for error
	// check opcode for ack, otherwise return 0
	if ((int)pkt_buf[0] == OP_DATA) {
		u_int8_t bytes[4];
		bytes[0] = pkt_buf[1];
		bytes[1] = pkt_buf[2];
		bytes[2] = pkt_buf[3];
		bytes[3] = pkt_buf[4];
		
		return reunite_bytes(bytes);
	} else {
		fprintf(stderr, "utils ~ get_data_client_id(): pkt_buf does not contain valid opcode to get a sequence number.\n");
		return 0;
	}
}

// returns sn of pkt_buf, 0 on error
u_int32_t get_wr_sn(char *pkt_buf) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ get_wr_sn(): cannot pass NULL ptr to pkt_buf.\n");
		return 0;
	}

	// pkt_sn occurs right after opcode for ack, not at all for error
	// check opcode for ack, otherwise return 0
	if ((int)pkt_buf[0] == OP_WR) {
		u_int8_t bytes[4];
		bytes[0] = pkt_buf[1];
		bytes[1] = pkt_buf[2];
		bytes[2] = pkt_buf[3];
		bytes[3] = pkt_buf[4];
		
		return reunite_bytes(bytes);
	} else {
		fprintf(stderr, "utils ~ get_wr_sn(): pkt_buf does not contain valid opcode to get a sequence number.\n");
		errno = 1;
		return 0;
	}
}

// returns pkt sn of pkt_buf if data pkt, 0 on error and sets errno to 1
u_int32_t get_data_sn(char *pkt_buf) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ get_data_sn(): cannot pass NULL ptr to pkt_buf.\n");
		return 0;
	}

	// pkt_sn occurs right after opcode for ack, not at all for error
	// check opcode for ack, otherwise return 0
	if ((int)pkt_buf[0] == OP_DATA) {
		u_int8_t bytes[4];
		bytes[0] = pkt_buf[5];
		bytes[1] = pkt_buf[6];
		bytes[2] = pkt_buf[7];
		bytes[3] = pkt_buf[8];
		
		return reunite_bytes(bytes);
	} else {
		fprintf(stderr, "utils ~ get_data_sn(): pkt_buf does not contain valid opcode to get a sequence number.\n");
		errno = 1;
		return 0;
	}
}

// returns payload size of pkt_buf if data pkt, 0xffffffff on error
u_int32_t get_data_pyld_sz(char *pkt_buf) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ get_data_pyld_sz(): cannot pass NULL ptr to pkt_buf.\n");
		return 0xffffffff;
	}

	// pkt_sn occurs right after opcode for ack, not at all for error
	// check opcode for ack, otherwise return 0
	if ((int)pkt_buf[0] == OP_DATA) {
		u_int8_t bytes[4];
		bytes[0] = pkt_buf[9];
		bytes[1] = pkt_buf[10];
		bytes[2] = pkt_buf[11];
		bytes[3] = pkt_buf[12];
		
		return reunite_bytes(bytes);
	} else {
		fprintf(stderr, "utils ~ get_data_pyld_sz(): pkt_buf does not contain valid opcode to get a sequence number.\n");
		return 0xffffffff;
	}
}

// returns pkt sn of pkt_buf, 0 on error
// can be used to get client ID from server, server assigns pkt_sn field to client ID when accepting handshake
u_int32_t get_ack_sn(char *pkt_buf) {
	if (pkt_buf == NULL) {
		fprintf(stderr, "utils ~ get_ack_sn(): cannot pass NULL ptr to pkt_buf.\n");
		return 0;
	}

	// pkt_sn occurs right after opcode for ack, not at all for error
	// check opcode for ack, otherwise return 0
	if ((int)pkt_buf[0] == OP_ACK) {
		u_int8_t bytes[4];
		bytes[0] = pkt_buf[1];
		bytes[1] = pkt_buf[2];
		bytes[2] = pkt_buf[3];
		bytes[3] = pkt_buf[4];
		
		return reunite_bytes(bytes);
	} else {
		fprintf(stderr, "utils ~ get_ack_sn(): pkt_buf does not contain valid opcode to get a sequence number.\n");
		errno = 1;
		return 0;
	}
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
				if (mkdir(outfile_dir, 0700) < 0) {
					fprintf(stderr, "utils ~ create_file_directory(): failed to make directory %s\n", outfile_dir);
					return -1;
				}
			}
		}
	}
	
	regfree(&regex_);

	return 0;
}
