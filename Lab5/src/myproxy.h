struct connection {
	int fd;
};

void usage(char *exec);

void sig_handler(int sig);

void handle_connection(struct connection conn);

void reload_forbidden_sites(void);
