# Simple Reliable File Transfer: Lab 3

## Student Info
Calvin Owen
1884240

## Files
### src/
<ins>myclient.c</ins> - C file implementing the main functionality of the client

<ins>myclient.h</ins> - Header file defining prototype functions for myclient.c

<ins>myserver.c</ins> - C file implementing the main functionality of the file transfer server

<ins>myserver.h</ins> - Header file defining prototype functions for myserver.c

<ins>utils.c</ins> - C file implementing utility functions to be used in myclient.c, myserver.c

<ins>utils.h</ins> - Header file defining prototype functions for utils.h


### test/
<ins>test_bad_inputs.sh</ins> - test myclient for bad inputs to see how it responds. expect exit with failure code.

<ins>test_high_drops.sh</ins> - test client response to server with high drop rate. expect it to exit with too many retransmissions.

<ins>test_multiple_clients.sh</ins> - test myserver to see if it can handle packets from multiple clients. expect unique files to be created and replicated correctly.

<ins>test_normal.sh</ins> - test myclient and myserver with normal valid inputs, confirm that file transfer happens safely and reliably.

<ins>test_server_crash.sh</ins> - test client reaction to a crashed server. expect it to exit with too many retransmissions or inability to detect server.

