/*
	defines constants used for the custom protocol implemented in Lab3: Simple Reliable File Transfer
*/

#define TIMEOUT_SECS 30
#define SN_BYTES 4												// num bytes for sequence number
#define CID_BYTES 4												// num bytes for client ID
#define PYLD_SZ_BYTES 4											// num bytes for payload size
#define DATA_HEADER_SIZE CID_BYTES + SN_BYTES + PYLD_SZ_BYTES	// header size for data packet
#define MAX_HEADER_SIZE DATA_HEADER_SIZE
#define MAX_SRVR_RES_SIZE CID_BYTES + 1							// max length of a packet sent from the server

enum OPCODES {
				OP_WR = 1,		// write request
				OP_ACK = 2,		// acknowledgment
				OP_DATA = 3,	// data included
				OP_ERROR = 4	// error
			};