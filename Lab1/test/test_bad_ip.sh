#!/usr/bin/env bash

echo "
!!! RUNNING TEST_BAD_IP !!!
"

if [ -f output.dat ]; then
	rm output.dat
fi

./bin/myweb www.example.com 0.0.0.0:/

if [ -f output.dat ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE
~~~~~~~~~~~~~~~~~~~~~~~"
else
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"
fi

echo "
!!! RUNNING TEST_BAD_PORT !!!
"

./bin/myweb www.example.com 93.184.216.34:-123/

if [ -f output.dat ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE
~~~~~~~~~~~~~~~~~~~~~~~"
	exit 1
fi

./bin/myweb www.example.com 93.184.216.34:1111111/

if [ -f output.dat ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE
~~~~~~~~~~~~~~~~~~~~~~~"
	exit 1
fi

./bin/myweb www.example.com 93.184.216.34:65535/

if [ -f output.dat ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE
~~~~~~~~~~~~~~~~~~~~~~~"
	exit 1
fi

echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"
