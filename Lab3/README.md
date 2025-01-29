# Simple Echo Server: Lab 2

## Student Info
Calvin Owen
1884240

## Files
### src/
<ins>myclient.c</ins> - C file implementing the main functionality of the client

<ins>myclient.h</ins> - Header file defining prototype functions for myclient.c

<ins>myserver.c</ins> - C file implementing the main functionality of the echo server

<ins>myserver.h</ins> - Header file defining prototype functions for myserver.c

<ins>oooserver.c</ins> - C file implementing functionality for a test server that returns packets out of order. Based on myserver.c

<ins>crashingserver.c</ins> - C file implementing functionality for a test server that crashes before all packets are echoed. Based on myserver.c

<ins>utils.c</ins> - C file implementing utility read/write functions to be used in myclient.c, myserver.c, oooserver.c, and crashingserver.c

<ins>utils.h</ins> - Header file defining prototype functions for utils.h


### test/
<ins>run_tests.sh</ins> - run all test in the folder

<ins>test_ascii.sh</ins> - test myserver and myclient to make sure that small and large files can be sent, echoed, and reconstructed correctly

<ins>test_bad_ip.sh</ins> - test myclient with a bad input ip address

<ins>test_multiple_clients.sh</ins> - test myserver to see if it can handle packets from multiple clients

<ins>test_ooo_server.sh</ins> - test myclient with oooserver to see if file can be reconstructed correctly if packets are received out of order from the server

<ins>test_server_crash.sh</ins> - test myclient to make sure nothing bad happens if it loses connection to the server in the middle of receiving packets

