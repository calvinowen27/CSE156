#define TIMEOUT_SECS 30
#define SN_BYTES 4			// num bytes for sequence number
#define CID_BYTES 4			// num bytes for client ID
#define PYLD_SZ_BYTES 4		// num bytes for payload size
#define MAX_HEADER_SIZE CID_BYTES + SN_BYTES + PYLD_SZ_BYTES

enum OPCODES {
				WR = 1,		// write request
				ACK = 2,	// acknowledgment
				DATA = 3,	// data included
				ERROR = 4	// error
			};