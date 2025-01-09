// struct for storing ip and document data after parsing
struct doc_data;

// prints correct program usage to stderr
void usage(char *exec);

// initialize socket with ip address and port, and return the file descriptor for the socket
// returns -1 on failure
int init_socket(const char *ip_addr, int port);

// parse document path/ip address field into doc_data struct for further use
// returns 0 on success, -1 on error
int parse_path(struct doc_data *data, const char *path);
