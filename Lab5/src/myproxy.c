#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <regex.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <netdb.h>
#include <sys/types.h>

#include "myproxy.h"
#include "utils.h"

static u_int32_t sig_queue = 0;

void usage(char *exec) {
	fprintf(stderr,
		"SYNOPSIS\n"
		"   Runs a simple HTTP <--> HTTPS proxy server.\n"
		"\n"
		"USAGE\n"
		"   %s [-p:a:l:] [-p listen port] [-a forbidden sites file path] [-l access log file path]\n"
		"\n"
		"OPTIONS\n"
		"   -p listen port  				port for proxy server to listen (defualt is 9090).\n"
		"   -a forbidden sites file path	path to file that holds forbidden site list.\n"
		"	-l access log file path			path to file where access log info should be written (default is \"access.log\").\n",
		exec);
}

int main (int argc, char **argv) {
	signal(SIGCHLD, &sig_catcher);
	signal(SIGINT, &sig_catcher);

	int port = 9090; // TODO: check
	char *forbidden_fp = NULL, *access_log_fp = "access.log";

	// parse args
	int opt = 0;
	while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
		switch (opt) {
		case 'p':
			port = atoi(optarg);
			if (port == 0 || port > 65535) {
				printf("Invalid port specified. Must be between 0 and 65535, (default 9090).\n");
				exit(1);
			}
		break;
		case 'a':
			forbidden_fp = optarg;
		break;
		case 'l':
			access_log_fp = optarg;
		break;
		default:
			usage(argv[0]); /* Invalid options, show usage */
		return EXIT_FAILURE;
		}
	}

	if (forbidden_fp == NULL) {
		printf("Need to specify forbidden sites file path using -a flag.\n");
		usage(argv[0]);
		exit(1);
	}

	// open files
	// int fbdn_fd = open(forbidden_fp, O_RDONLY, 0700);
	// if (fbdn_fd < 0) {
	// 	fprintf(stderr, "myproxy ~ main(): encountered error opening forbidden sites file %s: %s\n", forbidden_fp, strerror(errno));
	// 	exit(1);
	// }

	struct addrinfo *forbidden_addrs[NUM_FBDN_IPS] = { NULL };
	if (load_forbidden_ips(forbidden_fp, forbidden_addrs) < 0) {
		fprintf(stderr, "myproxy ~ main(): failed to load forbidden_ips from file: %s\n", forbidden_fp);
		exit(1);
	}

	int log_fd = open(access_log_fp, O_RDWR | O_CREAT, 0700); // TODO: make sure new entries are appended
	if (log_fd < 0) {
		fprintf(stderr, "myproxy ~ main(): encountered error opening access log file %s: %s\n", access_log_fp, strerror(errno));
		exit(1);
	}

	// init listen socket
	struct sockaddr_in sockaddr;
	socklen_t sockaddr_size = sizeof(sockaddr);

	int listen_fd = init_socket(&sockaddr, NULL, port, AF_INET, SOCK_STREAM, IPPROTO_TCP, true);
	if (listen_fd < 0) {
		fprintf(stderr, "myproxy ~ main(): failed to initialize socket with port %d: %s\n", port, strerror(errno));
		exit(1);
	}

	// set listen socket to nonblocking
	// so we can continuously poll signals
	if (fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		fprintf(stderr, "myproxy ~ main(): failed to set listen socket non-blocking\n");
		exit(1);
	}

	if (listen(listen_fd, 128) < 0) {
		fprintf(stderr, "myproxy ~ main(): failed to listen on port %d: %s\n", port, strerror(errno));
		exit(1);
	}

	int processes = 0;

	int connfd;
	while (1) {
		if (handle_sigs(&processes, forbidden_fp, forbidden_addrs) < 0) {
			fprintf(stderr, "myproxy ~ main(): encountered error handling queued signals.\n");
			exit(1); // TODO: cleanup?
		}

		if ((connfd = accept(listen_fd, (struct sockaddr *)&sockaddr, &sockaddr_size)) < 0 && errno != EAGAIN) {
			fprintf(stderr, "myproxy ~ main(): failed to accept connection on port %d: %s\n", port, strerror(errno));
			exit(1); // TODO: maybe don't exit? cleanup
		}

		if (connfd < 0) continue;

		if (processes == MAX_PROCESSES) {
			fprintf(stderr, "myproxy ~ main(): cannot accept connection, too many processes currently executing.\n");
			continue; // TODO: check?
		}

		// TODO: map child pid to connection ip
		if (fork() == 0) {
			struct connection conn;
			conn.fd = connfd;
			handle_connection(conn); // TODO: check error return? probably not, error handling internally
		} else {
			processes ++;

			printf("accepted connection, %d processes active\n", processes);
		}
	}

	// close files
	// close(fbdn_fd);
	close(log_fd);

	return 0;
}

void handle_connection(struct connection conn) {
	// make sure connection fd blocks for read
	if (fcntl(conn.fd, F_SETFL, fcntl(conn.fd, F_GETFL, 0) & ~O_NONBLOCK) < 0) {
		fprintf(stderr, "myproxy ~ main(): failed to set listen socket non-blocking\n");
		exit(1);
	}

	char buf[BUFFER_SIZE] = { 0 };
	// memset(buf, 0, sizeof(buf));
	// buf[BUFFER_SIZE] = 0; // just for printing, null termination

	// just read and print
	// while ((res = read(conn.fd, buf, sizeof(buf))) > 0) {
	// 	printf("%s", buf);

	// 	memset(buf, 0, sizeof(buf));
	// }

	int reg_res, prev_end = 0;
	regex_t reg;

	if ((reg_res = regcomp(&reg, REQLN_REGEX, REG_EXTENDED)) != 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): regcomp() failed for request line.\n");
		exit(1);
	}

	if (read_n_bytes(conn.fd, buf, sizeof(buf)) < 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to read_n_bytes() from connection fd.\n");
		exit(1);
	}

	printf("buf: %s\n", buf);

	regmatch_t pmatch[15];

	char *pkt_header = calloc(BUFFER_SIZE, sizeof(char));
	if (pkt_header == NULL) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to allocate pkt_header buffer.\n");
		exit(1);
	}

	if ((reg_res = regexec(&reg, buf, 15, pmatch, REG_EXTENDED)) != 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): regex() failed for request line.\n");

		if (reg_res != REG_NOMATCH) {	
			exit(1);
		}
	}

	if (reg_res == REG_NOMATCH) {
		fprintf(stderr, "myproxy ~ handle_connection(): didn't find match for request line...\n");
		exit(1);
	}

	prev_end = pmatch[0].rm_eo;

	char method[(pmatch[1].rm_eo - pmatch[1].rm_so) + 1];
	method[sizeof(method) - 1] = 0;
	memcpy(method, buf + pmatch[1].rm_so, sizeof(method) - 1);

	printf("method: %s\n", method);

	char url[(pmatch[2].rm_eo - pmatch[2].rm_so) + 1];
	url[sizeof(url) - 1] = 0;
	memcpy(url, buf + pmatch[2].rm_so, sizeof(url) - 1);

	printf("url: %s\n", url);

	char version[(pmatch[3].rm_eo - pmatch[3].rm_so) + 1];
	version[sizeof(version) - 1] = 0;
	memcpy(version, buf + pmatch[3].rm_so, sizeof(version) - 1);

	printf("version: %s\n", version);

	(void)prev_end;

	// read header into buffer and parse fields
	// resolve hostname and get ip
	// check if forbidden
	// connect to ip using SSL
	// while connection alive
		// forward packet to server
		// wait for response and decrypt
		// forward packet to client

	free(pkt_header);

	close(conn.fd);

	exit(0); // terminate process
}

int reload_forbidden_sites(void) {
	printf("reloading forbidden sites file !!!!!!!!!!!!!!!\n");

	return 0;
}

void sig_catcher(int sig) {
	sig_queue |= (((u_int32_t)1) << (sig - 1));
}

int handle_sigs(int *processes, const char *forbidden_fp, struct addrinfo **forbidden_addrs) {
	// check each queued signal and process
	if (sig_queued(SIGINT)) {
		if (load_forbidden_ips(forbidden_fp, forbidden_addrs) < 0) {
			fprintf(stderr, "myproxy ~ handle_sigs(): encountered error reloading forbidden sites file.\n");
			return -1;
		}
	}

	if (sig_queued(SIGCHLD)) {
		(*processes) --;
		printf("child process terminated. active processes: %d\n", *processes);
	}

	sig_queue = 0;

	return 0;
}

u_int32_t sig_queued(int sig) {
	return sig_queue & (((u_int32_t)1) << (sig - 1));
}

int load_forbidden_ips(const char *forbidden_fp, struct addrinfo **forbidden_addrs) {
	// reset array
	// for (int i = 0; i < NUM_FBDN_IPS; i++) {
	// 	if (forbidden_ips[i] != NULL) free(forbidden_ips[i]);
	// 	forbidden_ips[i] = NULL;
	// }

	int fd = open(forbidden_fp, O_RDONLY, 0700);
	if (fd < 0) {
		fprintf(stderr, "myproxy ~ load_forbidden_ips(): failed to open forbidden sites file path %s: %s\n", forbidden_fp, strerror(errno));
		return -1;
	}

	// read by line
	// check for valid ip or url
	// url --> resolve to ip
	// add to array

	char buf[257] = { 0 }, *line;
	off_t off = 0;
	int read_res, reg_res, addr_idx = 0;
	size_t line_idx = 0;

	bool comment_line = false, new_buf;

	regex_t ip_regex, domain_regex;
	regmatch_t pmatch;

	if (regcomp(&ip_regex, IP_REGEX, REG_EXTENDED) < 0) {
		fprintf(stderr, "myproxy ~ load_forbidden_ips(): failed to compile ip_regex for ip matching: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	if (regcomp(&domain_regex, DOMAIN_REGEX, REG_EXTENDED) < 0) {
		fprintf(stderr, "myproxy ~ load_forbidden_ips(): failed to compile domain_regex for ip matching: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	
	while ((read_res = read(fd, buf, sizeof(buf) - 1)) > 0) {
		new_buf = true;
		line_idx = 0;

		for (line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
			// printf("line: %s\n", line);
			char tline[strlen(line) + 2];
			strcpy(tline, line);
			tline[strlen(line)] = '\n';
			tline[strlen(line) + 1] = 0;
			
			// printf("tline: %s\n", tline);

			line_idx += strlen(tline);
			// printf("%zu\n\n", line_idx);
			if (strlen(tline) != line_idx && line_idx >= sizeof(buf) - 1) break;
			
			off += strlen(line);

			if (comment_line) {
				// printf("comment line");
				comment_line = new_buf;
				new_buf = false;
				if (comment_line) {
					// printf("\n");
					continue;
				}

				// printf(" end\n");
			}

			new_buf = false;

			if (tline[0] == '#') {
				// printf("comment line start\n");
				comment_line = true;
				continue;
			}
			
			if ((reg_res = regexec(&ip_regex, tline, 1, &pmatch, REG_EXTENDED)) != 0) {
				if (reg_res != REG_NOMATCH) {
					char errbuf[65] = { 0 };
					regerror(reg_res, &ip_regex, errbuf, sizeof(errbuf) - 1);
					// printf("%s\n", IP_REGEX);
					fprintf(stderr, "myproxy ~ load_forbidden_ips(): failed to execute ip_regex for ip matching %d: %s\n", reg_res, errbuf);
					close(fd);
					return -1;
				} else {
					if ((reg_res = regexec(&domain_regex, tline, 1, &pmatch, REG_EXTENDED)) != 0) {
						if (reg_res != REG_NOMATCH) {
							char errbuf[65] = { 0 };
							regerror(reg_res, &ip_regex, errbuf, sizeof(errbuf) - 1);
							// printf("%s\n", IP_REGEX);
							fprintf(stderr, "myproxy ~ load_forbidden_ips(): failed to execute domain_regex for ip matching %d: %s\n", reg_res, errbuf);
							close(fd);
							return -1;
						}

						if (!comment_line && new_buf) {
							fprintf(stderr, "Invalid formatting or ip in forbidden sites file.\n");
							return -1;
						}
						break;
					}
				}
			}

			// line_idx += 1;
			// printf("%zu\n", line_idx);
			off += 1;

			char *hostname = calloc(pmatch.rm_eo - pmatch.rm_so, sizeof(char));
			memcpy(hostname, tline + pmatch.rm_so, (pmatch.rm_eo - pmatch.rm_so) - 1);

			struct addrinfo *res, hints;

			// double check these
			hints.ai_family = PF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			hints.ai_flags = AI_DEFAULT;

			printf("resolving hostname %s\n", hostname);
			if (getaddrinfo(hostname, NULL, &hints, &res) < 0) {
				fprintf(stderr, "myproxy ~ load_forbidden_ips(): failed to resolve hostname %s\n", hostname);
				return -1;
			}

			if (res != NULL) {	
				u_int8_t *bytes = split_bytes((u_int32_t)((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);

				printf("%u.%u.%u.%u\n", bytes[3], bytes[2], bytes[1], bytes[0]);

				free(bytes);

				struct addrinfo *addr = malloc(sizeof(struct addrinfo));
				memcpy(addr, res, sizeof(struct addrinfo));

				forbidden_addrs[addr_idx] = addr;
				addr_idx ++;
				if (addr_idx == NUM_FBDN_IPS) {
					fprintf(stderr, "myproxy ~ load_forbidden_ips(): cannot load another ip from file, array full.\n");
					return -1;
				}
			}

			free(hostname);

			break;
		}

		lseek(fd, off, SEEK_SET);

		memset(buf, 0, sizeof(buf));
	}

	if (read_res < 0) {
		fprintf(stderr, "myproxy ~ load_forbidden_ips(): encountered error reading from file %s: %s\n", forbidden_fp, strerror(errno));
		close(fd);
		return -1;
	}

	regfree(&ip_regex);
	regfree(&domain_regex);
	close(fd);

	// printf("all forbidden ips loaded:\n");
	// for (int i = 0; i < NUM_FBDN_IPS; i++) {
	// 	if (forbidden_ips[i] == NULL) break;
	// 	printf("\t%s\n", forbidden_ips[i]);
	// }

	return 0;
}
