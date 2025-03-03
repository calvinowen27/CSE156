#ifndef MYPROXY_INCLUDE
#define MYPROXY_INCLUDE

struct connection {
	int fd;
};

void usage(char *exec);

void sig_catcher(int sig);
int handle_sigs(int *processes, const char *forbidden_fp, char **forbidden_ips);
u_int32_t sig_queued(int sig);

void handle_connection(struct connection conn);

int load_forbidden_ips(const char *forbidden_fp, char **forbidden_ips);

#endif
