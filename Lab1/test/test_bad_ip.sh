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
