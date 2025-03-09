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
#include <poll.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>

#include "myproxy.h"
#include "utils.h"

static u_int32_t sig_queue = 0;

void usage(char *exec) {
	fprintf(stderr,
		"SYNOPSIS\n"
		"   Runs a simple HTTP <--> HTTPS proxy server.\n"
		"\n"
		"USAGE\n"
		"   %s [-p:a:l:u] [-p listen port] [-a forbidden sites file path] [-l access log file path] [-u]\n"
		"\n"
		"OPTIONS\n"
		"   -p listen port  				port for proxy server to listen (defualt is 9090).\n"
		"   -a forbidden sites file path	path to file that holds forbidden site list.\n"
		"	-l access log file path			path to file where access log info should be written (default is \"access.log\").\n"
		"	-untrusted						allow proxy to connect to untrusted IPs.\n",
		exec);
}

int main (int argc, char **argv) {
	signal(SIGCHLD, &sig_catcher);
	signal(SIGINT, &sig_catcher);

	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	int port = 9090; // TODO: check
	char *forbidden_fp = NULL, *access_log_fp = "access.log";

	bool trusting = false;

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
			case 'u':
				trusting = true;
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
	struct sockaddr_in proxyaddr;
	memset(&proxyaddr, 0, sizeof(proxyaddr));

	int listen_fd = init_socket(&proxyaddr, NULL, port, AF_INET, SOCK_STREAM, IPPROTO_TCP, true);
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

	int clientfd;
	while (1) {
		if (handle_sigs(&processes, forbidden_fp, forbidden_addrs) < 0) {
			fprintf(stderr, "myproxy ~ main(): encountered error handling queued signals.\n");
			exit(1); // TODO: cleanup?
		}

		struct sockaddr_in clientaddr;
		memset(&clientaddr, 0, sizeof(clientaddr));
		socklen_t clientaddr_size = sizeof(struct sockaddr_in);
		
		if ((clientfd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientaddr_size)) < 0 && errno != EAGAIN) {
			fprintf(stderr, "myproxy ~ main(): failed to accept connection on port %d: %s\n", port, strerror(errno));
			exit(1); // TODO: maybe don't exit? cleanup
		}

		if (clientfd < 0) continue;

		if (processes == MAX_PROCESSES) {
			fprintf(stderr, "myproxy ~ main(): cannot accept connection, too many processes currently executing.\n");
			continue; // TODO: check?
		}

		struct connection conn;
		conn.pkt_header = NULL;

		conn.clientaddr = clientaddr;

		conn.clientfd = clientfd;

		if (init_connection(&conn, clientfd, clientaddr, trusting) < 0) {
			fprintf(stderr, "myproxy ~ main(): failed to initialize connection.\n");
			close_connection(&conn, 1);
		}

		// TODO: map child pid to connection ip
		if (fork() == 0) {
			handle_connection(&conn, forbidden_addrs); // TODO: check error return? probably not, error handling internally
			close_connection(&conn, 0);
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

int init_connection(struct connection *conn, int clientfd, struct sockaddr_in clientaddr, bool trusting) {
	conn->clientfd = clientfd;
	conn->clientaddr = clientaddr;

	conn->pkt_header = NULL;

	conn->xff_field = false;

	// save ip of client
	conn->client_ip = get_addr_ipv4(&clientaddr);
	if (!conn->client_ip) {
		fprintf(stderr, "myproxy ~ init_connection(): failed to get client ip string.\n");
		return -1;
	}

	conn->trusting = trusting;

	// initialize ctx
	const SSL_METHOD *ssl_method = SSLv23_client_method();
	conn->ctx = SSL_CTX_new(ssl_method);

	if (!conn->ctx) {
		fprintf(stderr, "myproxy ~ init_connection(): failed to initialize SSL CTX.\n");
		return -1;
	}

	SSL_CTX_set_options(conn->ctx, SSL_OP_ALL | SSL_OP_NO_SSLv2);

	// initialize ssl
	conn->ssl = SSL_new(conn->ctx);

	if (!conn->ssl) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to initialize SSL object.\n");
		return -1;
	}

	return 0;
}

void free_connection(struct connection *conn) {
	if (conn->client_ip) free(conn->client_ip);
	conn->client_ip = NULL;

	if (conn->pkt_header) free(conn->pkt_header);
	conn->pkt_header = NULL;

	if (conn->ctx) SSL_CTX_free(conn->ctx);
	conn->ctx = NULL;

	if (conn->ssl) SSL_free(conn->ssl);
	conn->ssl = NULL;

	regfree(&conn->reg);
}

void handle_connection(struct connection *conn, struct addrinfo **forbidden_addrs) {
	// make sure connection clientfd blocks for read
	if (fcntl(conn->clientfd, F_SETFL, fcntl(conn->clientfd, F_GETFL, 0) & ~O_NONBLOCK) < 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to set listen socket non-blocking\n");
		close_connection(conn, 1);
	}

	char buf[BUFFER_SIZE + 1] = { 0 };
	
	int prev_end = 0;

	int bytes_read;
	if ((bytes_read = read_n_bytes(conn->clientfd, buf, sizeof(buf))) < 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to read_n_bytes() from connection clientfd.\n");
		close_connection(conn, 1);
		// exit(1);
	}

	printf("buf: %s\n", buf);

	conn->pkt_header = calloc(BUFFER_SIZE, sizeof(char));
	conn->pkt_header_size = BUFFER_SIZE;
	if (!conn->pkt_header) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to allocate pkt_header buffer.\n");
		close_connection(conn, 1);
	}

	prev_end += parse_req_line(conn, buf);

	char *serv_ip = NULL;
	prev_end += parse_header_fields(conn, buf + prev_end, forbidden_addrs, &serv_ip);

	if (!serv_ip) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to get server ip from header fields.\n");
		close_connection(conn, 1);
	}

	printf("SERVER IP: %s\n", serv_ip);

	if (!conn->xff_field) {
		char xff_hf[56] = { 0 };
		
		// TODO: check for more proxies desired?
		// TODO: make sure this gets appended if header field already exists
		sprintf(xff_hf, "X-Forwarded-For: %s" DOUBLE_EMPTY_LINE, conn->client_ip);
		
		if (append_buf(&conn->pkt_header, &conn->pkt_header_size, xff_hf, strlen(xff_hf)) < 0) {
			fprintf(stderr, "myproxy ~ handle_connection(): failed to add X-Forward-For header field to pkt header.\n");
			close_connection(conn, 1);
		}

		conn->xff_field = true;
		prev_end += strlen(xff_hf);
	}

	printf("\nHEADER:\n==============\n%s\n==============\n", conn->pkt_header);

	// find start of body from first packet
	char *pkt_body_start = buf + prev_end + 2;

	connect_to_server(conn, serv_ip);
	free(serv_ip);

	printf("SSL connection complete.\n");

	if (!verify_peer_cert(conn)) {
		fprintf(stderr, "myproxy ~ handle_connection(): could not verify peer certificate was trustworthy.\n");
		send_response(conn, HTTP_BAD_GATEWAY);
		close_connection(conn, 1);
	}

	int pkt_header_len = prev_end;
	char pkt[pkt_header_len + strlen(pkt_body_start) + 1];
	pkt[sizeof(pkt) - 1] = 0;
	memcpy(pkt, conn->pkt_header, pkt_header_len);
	memcpy(pkt + pkt_header_len, pkt_body_start, strlen(pkt_body_start));
	printf("pkt header size: %zu strlen(pkt header): %zu\n", sizeof(pkt) - 1, strlen(pkt));

	printf("\n\n====================\nCLIENT:\n%s\n====================\n", pkt);

	int ssl_res;

	if ((ssl_res = SSL_write(conn->ssl, pkt, sizeof(pkt) - 1)) <= 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to write to SSL connection. %d: %s\n", SSL_get_error(conn->ssl, ssl_res), strerror(errno));
		close_connection(conn, 1);
	}

	perform_proxy(conn);

	close_connection(conn, 0);

	printf("\n!!!!!!!!!!!!!!!\nCONNECTION CLOSED\n!!!!!!!!!!!!!!!\n");
}

int parse_req_line(struct connection *conn, char *pkt_buf) {
	regmatch_t pmatch[4];
	
	int reg_res;

	if ((reg_res = regcomp(&conn->reg, REQLN_REGEX, REG_EXTENDED)) != 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): regcomp() failed for request line.\n");
		close_connection(conn, 1);
	}

	if ((reg_res = regexec(&conn->reg, pkt_buf, 4, pmatch, 0)) != 0) {
		fprintf(stderr, "myproxy ~ parse_req_line(): regex() failed for request line.\n");

		if (reg_res != REG_NOMATCH) {	
			close_connection(conn, 1);
		}
	}

	if (reg_res == REG_NOMATCH) {
		fprintf(stderr, "myproxy ~ handle_connection(): didn't find match for request line...\n");
		close_connection(conn, 1);
	}

	char method[(pmatch[1].rm_eo - pmatch[1].rm_so) + 1];
	method[sizeof(method) - 1] = 0;
	memcpy(method, pkt_buf + pmatch[1].rm_so, sizeof(method) - 1);

	printf("method: %s\n", method);

	char url[(pmatch[2].rm_eo - pmatch[2].rm_so) + 1];
	url[sizeof(url) - 1] = 0;
	memcpy(url, pkt_buf + pmatch[2].rm_so, sizeof(url) - 1);

	printf("url: %s\n", url);

	char version[(pmatch[3].rm_eo - pmatch[3].rm_so) + 1];
	version[sizeof(version) - 1] = 0;
	memcpy(version, pkt_buf + pmatch[3].rm_so, sizeof(version) - 1);

	if (strcmp(version, "HTTP/1.1")) {
		fprintf(stderr, "myproxy ~ handle_connection(): version not supported: %s\n", version);
		send_response(conn, HTTP_VERSION_NO_SUPPORT);
		close_connection(conn, 1);
	}

	printf("version: %s\n", version);

	if (append_buf(&conn->pkt_header, &conn->pkt_header_size, pkt_buf + pmatch[0].rm_so, pmatch[0].rm_eo - pmatch[0].rm_so) < 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to append request line to header buffer.\n");
		close_connection(conn, 1);
	}

	// check for unimplemented methods
	if (strcmp("GET", method) && strcmp("HEAD", method)) {	
		send_response(conn, HTTP_NOT_IMPLEMENTED);
		close_connection(conn, 1);
	}

	return pmatch[0].rm_eo;
}

int parse_header_fields(struct connection *conn, char *pkt_buf, struct addrinfo **forbidden_addrs, char **host_ipv4) {
	if (regcomp(&conn->reg, HEADER_FIELD_REGEX, REG_EXTENDED) < 0) {
		fprintf(stderr, "myproxy ~ parse_header_fields(): regcomp() failed for header fields.\n");
		close_connection(conn, 1);
	}

	printf("header fields:\n");

	struct addrinfo *hostaddr = NULL;

	int reg_res, end = 0; //, content_len = -1;

	regmatch_t pmatch[3];

	while ((reg_res = regexec(&conn->reg, pkt_buf + end, 3, pmatch, 0)) == 0) {
		// parse header field for processing
		char key[(pmatch[1].rm_eo - pmatch[1].rm_so) + 1];
		key[sizeof(key) - 1] = 0;
		memcpy(key, pkt_buf + end + pmatch[1].rm_so, sizeof(key) - 1);

		char val[(pmatch[2].rm_eo - pmatch[2].rm_so) + 1];
		val[sizeof(val) - 1] = 0;
		memcpy(val, pkt_buf + end + pmatch[2].rm_so, sizeof(val) - 1);

		printf("\t%s: %s\n", key, val);

		// add header field back to pkt header for forwarding
		if (append_buf(&conn->pkt_header, &conn->pkt_header_size, pkt_buf + end + pmatch[0].rm_so, pmatch[0].rm_eo - pmatch[0].rm_so) < 0) {
			fprintf(stderr, "myproxy ~ parse_header_fields(): failed to append header field to header buffer.\n");
			close_connection(conn, 1);
		}

		// resolve host ip and check if forbidden
		if (!strcmp("Host", key)) {
			if (resolve_host(val, &hostaddr) < 0) {
				fprintf(stderr, "myproxy ~ parse_header_fields(): failed to resolve host.\n");
				send_response(conn, HTTP_BAD_GATEWAY);
				close_connection(conn, 1);
			}

			if (host_forbidden(hostaddr, forbidden_addrs)) {
				send_response(conn, HTTP_FORBIDDEN);
				close_connection(conn, 1);
			}

			(*host_ipv4) = get_addr_ipv4((struct sockaddr_in *)(hostaddr->ai_addr));
		} else if(!strcmp("X-Forwarded-For", key)) { // add client ip to list
			char xff_append[strlen(conn->client_ip) + 2];
			sprintf(xff_append, ", %s", conn->client_ip);
			if (append_buf(&conn->pkt_header, &conn->pkt_header_size, xff_append, strlen(conn->client_ip) + 2) < 0) {
				fprintf(stderr, "myproxy ~ parse_header_fields(): failed to append client ip to X-Forwarded-For header field.\n");
				close_connection(conn, 1);
			}
			conn->xff_field = true;
			end += sizeof(xff_append);
		}
		// else if (!strcmp("Content-Length", key)) { // save content length
		// 	content_len = atoi(val);
		// }

		end += pmatch[0].rm_eo;
	}

	if (reg_res != REG_NOMATCH) {
		fprintf(stderr, "myproxy ~ parse_header_fields(): regexec() failed for header fields.\n");
		close_connection(conn, 1);
	}

	return end;
}

void connect_to_server(struct connection *conn, char *serv_ip) {
	struct sockaddr_in servaddr;

	conn->servfd = init_socket(&servaddr, serv_ip, 443, AF_INET, SOCK_STREAM, 0, false);
	if (conn->servfd < 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to initialize server socket.\n");
		close_connection(conn, 1);
	}

	if (connect(conn->servfd, (struct sockaddr *)&servaddr, sizeof(struct sockaddr)) < 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to connect to server.\n");
		close_connection(conn, 1);
	}

	if (!SSL_set_fd(conn->ssl, conn->servfd)) {
		fprintf(stderr, "myproxy ~ handle_connection(): failed to set ssl clientfd\n");
		close_connection(conn, 1);
	}

	int ssl_res;

	printf("calling SSL_connect()\n");

	if ((ssl_res = SSL_connect(conn->ssl)) <= 0) {
		send_response(conn, HTTP_GATEWAY_TIMEOUT);
		fprintf(stderr, "myproxy ~ handle_connection(): failed to initiate SSL connect %d (%d).\n", SSL_get_error(conn->ssl, ssl_res), ssl_res);
		close_connection(conn, 1);
	}
}

bool verify_peer_cert(struct connection *conn) {
	X509 *cert = SSL_get_peer_certificate(conn->ssl);
	if (cert == NULL) {
		fprintf(stderr, "myproxy ~ handle_connection(): server did not provide certificate.\n");
		close_connection(conn, 1);
	}

	if (conn->trusting) return true;

	STACK_OF(X509) *sk = SSL_get_peer_cert_chain(conn->ssl);
	if (sk == NULL) {
		sk = sk_X509_new_null();
		sk_X509_push(sk, cert);
	}

	X509 *subj = cert;
	X509 *check_cert;

	int ca = -1;

	while ((check_cert = sk_X509_pop(sk)) != NULL) {
		if (X509_check_issued(check_cert, subj) == X509_V_OK) {
			if ((ca = X509_check_ca(check_cert)) >= 1) {
				printf("Verified CA with unbroken chain: %d\n", ca);
				return true;
			}

			subj = check_cert;
		} else {
			fprintf(stderr, "myproxy ~ handle_connection(): certificate chain broken.\n");
			break;
		}

		if (check_cert == subj) {
			printf("Self issued certificate.\n");
			break;
		}
	}

	return false;
}

void perform_proxy(struct connection *conn) {
	int bytes_read, bytes_written, ssl_res, res;

	int serv_conn_closed = 0, client_conn_closed = 0;

	int bytes_written;

	char buf[BUFFER_SIZE + 1] = { 0 };
	char client_buf[BUFFER_SIZE + 1] = { 0 };
	char serv_buf[BUFFER_SIZE + 1] = { 0 };

	// struct pollfd client_poll = { conn->clientfd, POLLIN, 1000 };
	struct pollfd serv_poll = { conn->servfd, POLLIN, 0 };

	while (!serv_conn_closed && !client_conn_closed) {
		do {
			if (poll(&serv_poll, 1, 1000) > 0) {
				printf("reading from server...\n");
				memset(serv_buf, 0, sizeof(serv_buf));
				if ((bytes_read = SSL_read(conn->ssl, serv_buf, sizeof(serv_buf) - 1)) < 0) {
					fprintf(stderr, "myproxy ~ handle_connection(): failed to read from SSL connection. %d: %s\n", SSL_get_error(conn->ssl, bytes_read), strerror(errno));
					close_connection(conn, 1);
				}
			} else {
				printf("nothing available from server...\n");
				if ((bytes_written = SSL_write(conn->ssl, client_buf, bytes_read)) <= 0) {
					fprintf(stderr, "myproxy ~ handle_connection(): failed to write to SSL connection. %d: %s\n", SSL_get_error(conn->ssl, bytes_written), strerror(errno));
					serv_conn_closed = 1;
				}
			}
			
			if (bytes_read == 0 || serv_conn_closed) {
				printf("breaking\n");
				serv_conn_closed = 1;
				break;
			}

			printf("\n\n====================\nSERVER:\n%s\n====================\n", serv_buf);

			if ((bytes_written = write_n_bytes(conn->clientfd, serv_buf, bytes_read)) < 0) {
				fprintf(stderr,  "myproxy ~ handle_connection(): failed to write to client.\n");
				client_conn_closed = 1;
			}
		} while (bytes_written > 0 && bytes_read > 0);

		if (serv_conn_closed || client_conn_closed) break;

		do {
			memset(buf, 0, sizeof(buf));
			if ((res = read_n_bytes(conn->clientfd, buf, sizeof(buf) - 1)) < 0) {
				fprintf(stderr, "myproxy ~ handle_connection(): failed to read from client.\n");
				close_connection(conn, 1);
			}

			if (res == 0) {
				client_conn_closed = 1;
				break;
			}

			printf("\n\n====================\nCLIENT:\n%s\n====================\n", buf);

			if ((ssl_res = SSL_write(conn->ssl, buf, res)) < 0) {
				fprintf(stderr, "myproxy ~ handle_connection(): failed to write to SSL connection. %d: %s\n", SSL_get_error(conn->ssl, ssl_res), strerror(errno));
				// close_connection(conn, 1);
				serv_conn_closed = 1;
			}
		} while (ssl_res > 0 && res > 0);
	}
}

int send_response(struct connection *conn, int status_code) {
	char resp_buf[BUFFER_SIZE] = { 0 };

	char *stat_phrase = NULL;

	switch (status_code) {
		case HTTP_FORBIDDEN:
			stat_phrase = "Forbidden";
		break;
		case HTTP_NOT_IMPLEMENTED:
			stat_phrase = "Not Implemented";
		break;
		case HTTP_BAD_GATEWAY:
			stat_phrase = "Bad Gateway";
		break;
		case HTTP_GATEWAY_TIMEOUT:
			stat_phrase = "Gateway Timeout";
		break;
		case HTTP_VERSION_NO_SUPPORT:
			stat_phrase = "HTTP Version Not Supported";
		break;
		default:
			fprintf(stderr, "myproxy ~ send_reponse(): status code %d not recognized.\n", status_code);
			return -1;
		break;
	};

	sprintf(resp_buf, "HTTP/1.1 %d %s" EMPTY_LINE "Content-Length: %lu" DOUBLE_EMPTY_LINE "%s", status_code, stat_phrase, strlen(stat_phrase), stat_phrase);

	if (write_n_bytes(conn->clientfd, resp_buf, strlen(resp_buf)) < 0) {
		fprintf(stderr, "myproxy ~ send_response(): failed to write response to connection clientfd.\n");
		return -1;
	}

	return 0;
}

int resolve_host(char *hostname, struct addrinfo **res) {
	struct addrinfo hints;

	// double check these
	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	// hints.ai_flags = 0x00000200 | 0x00000400;

	printf("resolving hostname %s\n", hostname);
	if (getaddrinfo(hostname, NULL, &hints, res) < 0) {
		fprintf(stderr, "myproxy ~ load_forbidden_ips(): failed to resolve hostname %s\n", hostname);
		return -1;
	}

	return 0;
}

int host_forbidden(struct addrinfo *host, struct addrinfo **forbidden_addrs) {
	printf("checking forbidden host\n");

	if (host == NULL) {
		fprintf(stderr, "myproxy ~ host_forbidden(): host ptr is NULL.\n");
		return -1;
	}

	if (forbidden_addrs == NULL) {
		fprintf(stderr, "myproxy ~ host_forbidden(): forbidden_addrs is NULL.\n");
		return -1;
	}

	struct addrinfo *addr;
	for (int i = 0; i < NUM_FBDN_IPS; i++) {
		addr = forbidden_addrs[i];

		// printf("forbidden_addr[%d] is %s null\n", i, addr == NULL ? "" : "not");

		if (addr == NULL) break;

		do {
			// printf("do\n");
			if (sockaddrs_eq(*addr->ai_addr, *host->ai_addr)) {
				// printf("equal\n");
				return 1;
			}

			// printf("sockaddr not equal\n");

			addr = addr->ai_next;
		} while (addr != NULL);

		// printf("ok\n");
	}

	return 0;
}

void close_connection(struct connection *conn, int exit_code) {
	if (!conn) {
		fprintf(stderr, "myproxy ~ close_connection(): cannot close connection for NULL connection ptr.\n");
		exit(1);
	}

	close(conn->clientfd);
	close(conn->servfd);

	SSL_shutdown(conn->ssl);
	
	free_connection(conn);
	
	exit(exit_code);
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

			struct addrinfo *res = NULL;

			if (resolve_host(hostname, &res) < 0) {
				fprintf(stderr, "myproxy ~ handle_connection(): failed to resolve host.\n");
				exit(1);
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
