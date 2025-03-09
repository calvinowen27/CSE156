#ifndef MYPROXY_INCLUDE
#define MYPROXY_INCLUDE

#define OPTIONS "p:a:l:u"
#define MAX_PROCESSES 50
#define BUFFER_SIZE 4096
#define NUM_FBDN_IPS 1000

#define HTTPS_PORT 443

#define IP_REGEX "(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9][0-9]|[0-9])(\n+)"
#define DOMAIN_REGEX "(([a-zA-Z0-9\\-_])+)((\\.[a-zA-Z0-9\\-_/]+)+)(\n+)"

#define EMPTY_LINE "\r\n"
#define DOUBLE_EMPTY_LINE EMPTY_LINE EMPTY_LINE

#define METHOD_REGEX	"([a-zA-Z]{3,8})"
// #define URL_REGEX 		"(((http|https)://)?(([a-zA-Z0-9\\-_])+)((\\.[a-zA-Z0-9\\-_/]+)+))"
#define URL_REGEX		"([a-zA-Z0-9\\-_/:?\\.]+)"
#define VERSION_REGEX	"(HTTP/[0-9].[0-9])"
#define REQLN_REGEX		METHOD_REGEX " " URL_REGEX " " VERSION_REGEX EMPTY_LINE

#define HKEY_REGEX			"([a-zA-Z0-9.-]{1,128})"
#define HVAL_REGEX			"([ -~]{1,128})"
#define HEADER_FIELD_REGEX	HKEY_REGEX ": " HVAL_REGEX EMPTY_LINE

struct connection {
	int clientfd;
	struct sockaddr_in clientaddr;

	int servfd;

	char *pkt_header;
	ssize_t pkt_header_size;
	regex_t reg;

	char *proxy_ip;
	char *client_ip;

	bool trusting;

	SSL *ssl;
	SSL_CTX *ctx;
};

void usage(char *exec);

void sig_catcher(int sig);
int handle_sigs(int *processes, const char *forbidden_fp, struct addrinfo **forbidden_addrs);
u_int32_t sig_queued(int sig);

void handle_connection(struct connection *conn, struct addrinfo **forbidden_addrs);

int send_response(struct connection *conn, int status_code);

int init_connection(struct connection *conn, int clientfd, struct sockaddr_in proxyaddr, struct sockaddr_in clientaddr, bool trusting);
void free_connection(struct connection *conn);

int resolve_host(char *hostname, struct addrinfo **res);
int host_forbidden(struct addrinfo *host, struct addrinfo **forbidden_addrs);

void close_connection(struct connection *conn, int exit_code);

int load_forbidden_ips(const char *forbidden_fp, struct addrinfo **forbidden_addrs);

#endif
