
// read n bytes into buf from sockfd
// return 0 on success, -1 for error
int read_n_bytes(int sockfd, const char *buf, int n);

// read from sockfd into buf until match regular expression is found
// return number of bytes read, or -1 for error
int read_until(int sockfd, const char *buf, const char *match, int match_len, int max_chars);
