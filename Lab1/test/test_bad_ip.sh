#!/usr/bin/env bash

make clean
make

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
