# Simple Web Downloader: Lab 1

## Student Info
Calvin Owen
1884240

## Files
### src/
<ins>myweb.c</ins> - C file implementing the main functionality of the web downloader

<ins>myweb.h</ins> - Header file defining prototype functions for myweb.c

<ins>utils.c</ins> - C file implementing utility read/write functions to be used in myweb.c

<ins>utils.h</ins> - Header file defining prototype functions for utils.h


### test/
<ins>run_tests.sh</ins> - run all test in the folder

<ins>test_bad_ip.sh</ins> - test myweb app with an invalid ip address and invalid ports

<ins>test_403.sh</ins> - test myweb app with a request to a forbidden resource, to make sure output is consistent

<ins>test_chunked.sh</ins> - test whether or not 'Transfer-Encoding: chunked' header field is supported

<ins>test_example.sh</ins> - test reasonable commands for multiple websites to make sure outputs match with expected from equivalent curl command

<ins>test_header.sh</ins> - test the -h flag for the myweb, which should output the response header to stdout, checks for match with equivalent curl command

