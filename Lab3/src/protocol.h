/*
	defines constants used for the custom protocol implemented in Lab3: Simple Reliable File Transfer
*/

#define TIMEOUT_SECS 5												// second until client retransmits packet window to server
#define LOSS_TIMEOUT_SECS 5											// seconds until server accepts packet loss and sends ack
#define SN_BYTES 4													// num bytes for sequence number
#define CID_BYTES 4													// num bytes for client ID
#define PYLD_SZ_BYTES 4												// num bytes for payload size
#define WINSZ_BYTES 4												// num bytes for window size
#define DATA_HEADER_SIZE CID_BYTES + SN_BYTES + PYLD_SZ_BYTES + 1	// header size for data packet
#define WR_HEADER_SIZE WINSZ_BYTES + 1
#define MAX_HEADER_SIZE DATA_HEADER_SIZE
#define MAX_SRVR_RES_SIZE CID_BYTES + 1								// max length of a packet sent from the server

enum OPCODES {
				OP_WR = 1,		// write request
				OP_ACK = 2,		// acknowledgment
				OP_DATA = 3,	// data included
				OP_ERROR = 4	// error
			};
