struct connection {
	int fd;
};

struct forbidden_ips {
	int len;
	char **ips;
};

void usage(char *exec);

void sig_catcher(int sig);
void handle_sigs(int *processes);
int sig_queued(int sig);

void handle_connection(struct connection conn);

struct forbidden_ips *load_forbidden_ips(const char *forbidden_fp);
void reload_forbidden_sites(void);
