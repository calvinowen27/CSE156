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

#include "myproxy.h"
#include "utils.h"

#define OPTIONS "p:a:l:"
#define MAX_PROCESSES 50
#define BUFFER_SIZE 4096

static int processes = 0; // TODO: ?

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
	signal(SIGCHLD, &sig_handler);
	signal(SIGINT, &sig_handler);

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
	int fbdn_fd = open(forbidden_fp, O_RDONLY, 0700);
	if (fbdn_fd < 0) {
		fprintf(stderr, "myproxy ~ main(): encountered error opening forbidden sites file %s: %s\n", forbidden_fp, strerror(errno));
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

	if (listen(listen_fd, 128) < 0) {
		fprintf(stderr, "myproxy ~ main(): failed to listen on port %d: %s\n", port, strerror(errno));
		exit(1);
	}

	// int processes = 0;

	int connfd;
	while (1) {
		if ((connfd = accept(listen_fd, (struct sockaddr *)&sockaddr, &sockaddr_size)) < 0) {
			fprintf(stderr, "myproxy ~ main(): failed to accept connection on port %d: %s\n", port, strerror(errno));
			exit(1); // TODO: maybe don't exit? cleanup
		}

		if (processes == MAX_PROCESSES) {
			fprintf(stderr, "myproxy ~ main(): cannot accept connection, too many processes currently executing.\n");
			continue; // TODO: check?
		}

		if (fork() == 0) {
			struct connection conn;
			conn.fd = connfd;
			handle_connection(conn); // TODO: check error return? probably not, error handling internally
		}

		processes ++;
	}

	// close files
	close(fbdn_fd);
	close(log_fd);

	return 0;
}

void handle_connection(struct connection conn) {
	char buf[BUFFER_SIZE + 1];
	memset(buf, 0, sizeof(buf));
	// buf[BUFFER_SIZE] = 0; // just for printing, null termination

	int res;

	// just read and print
	while ((res = read(conn.fd, buf, sizeof(buf))) > 0) {
		printf("%s", buf);

		memset(buf, 0, sizeof(buf));
	}

	if (res < 0) {
		fprintf(stderr, "myproxy ~ handle_connection(): encountered error reading from connection: %s\n", strerror(errno));
		exit(1);
	}

	exit(0); // terminate process
}

void reload_forbidden_sites(void) {
	printf("reloading forbidden sites file !!!!!!!!!!!!!!!\n");
}

void sig_handler(int sig) {
	switch (sig) {
		case SIGCHLD:
			processes --;
			printf("child process terminated. active processes: %d\n", processes);
		break;
		case SIGINT:
			reload_forbidden_sites();
		break;
	};
}
