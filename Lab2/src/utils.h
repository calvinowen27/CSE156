void logerr(const char *);

// split uint32_t into uint8_t[4]
uint8_t *split_bytes(uint32_t val);

uint32_t reunite_bytes(uint8_t *bytes);

// write n bytes from buf into sockfd
// returns bytes written on success, -1 on error
int write_n_bytes(int sockfd, char *buf, int n);
