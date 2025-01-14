#!/usr/bin/env bash

make clean
make

if [ -f output.dat ]; then
	rm output.dat
fi

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
