typedef u_int32_t;

struct connection {
	int fd;
};

void usage(char *exec);

void sig_catcher(int sig);
void handle_sigs(int *processes);
u_int32_t sig_queued(int sig);

void handle_connection(struct connection conn);

int load_forbidden_ips(const char *forbidden_fp);
int reload_forbidden_sites(void);
