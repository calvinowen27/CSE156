// read into buf from sockfd. reads n bytes or the size of buf, whichever is smaller
// return bytes read on success, -1 for error
int read_n_bytes(int sockfd, char *buf, int n);

// write n bytes from buf into sockfd
// returns bytes written on success, -1 on error
int write_n_bytes(int sockfd, char *buf, int n);

// read from sockfd into buf until match regular expression is found
// return number of bytes read, or -1 for error
int read_until(int sockfd, char *buf, int buf_len, const char *match);

// continually read bytes from infd and write them to outfd, until n bytes have been passed
// return 0 on success, -1 for error
int pass_n_bytes(int infd, int outfd, int n);

// continually read bytes from infd and write them to outfd, until EOF is reached
// return 0 on success, -1 for error
int pass_file(int infd, int outfd);
