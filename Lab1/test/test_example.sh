#!/usr/bin/env bash

echo "
!!! RUNNING TEST_EXAMPLE !!!
"

./bin/myweb www.example.com 93.184.216.24:80/index.html
curl 93.184.216.34:80/index.html -H "Host: www.example.com" -o output1
diff output.dat output1 > diff

if [ -s diff ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: www.example.com
~~~~~~~~~~~~~~~~~~~~~~~"
	exit 1
fi

./bin/myweb www.sofwareqatest.com 66.39.40.38:80/index.html
curl 66.39.40.38:80/index.html -H "Host: www.sofwareqatest.com" -o output1
diff output.dat output1 > diff

if [ -s diff ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: www.sofwareqatest.com
~~~~~~~~~~~~~~~~~~~~~~~"
	exit 1
fi

echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"

rm output1 diff
