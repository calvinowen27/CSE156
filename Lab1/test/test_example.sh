#!/usr/bin/env bash

make clean
make

./bin/myweb www.example.com 93.184.216.24:80/index.html
curl 93.184.216.34:80/index.html -H "Host: www.example.com" -o output1
diff output.dat output1 > diff

if [ -s diff ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE
~~~~~~~~~~~~~~~~~~~~~~~"
fi

./bin/myweb www.neverssl.com 34.223.124.45:80/index.html
curl 34.223.124.45:80/index.html -H "Host: www.neverssl.com" -o output1
diff output.dat output1 > diff

if [ -s diff ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE
~~~~~~~~~~~~~~~~~~~~~~~"
fi

rm output1 diff
