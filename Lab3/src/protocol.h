#define TIMEOUT_SECS 30
#define SN_BYTES 4
#define CID_BYTES 4
#define PD_SZ_BYTES 4

enum OPCODES {
				WR = 1,		// write request
				ACK = 2,	// acknowledgment
				DATA = 3,	// data included
				ERROR = 4	// error
			};