#!/usr/bin/env bash

make clean
make

./bin/myweb www.example.com 93.184.216.24:80/index.html -h > head1
curl 93.184.216.24:80/index.html -D head2 -H "Host: www.example.com" -o out
diff head1 head2 > diff

if [ -s diff ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: www.example.com
~~~~~~~~~~~~~~~~~~~~~~~"
	exit 1
fi

./bin/myweb www.sofwareqatest.com 66.39.40.38:80/index.html -h > head1
curl 66.39.40.38:80/index.html -D head2 -H "Host: www.sofwareqatest.com" -o out
diff head1 head2 > diff

if [ -s diff ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: www.sofwareqatest.com
~~~~~~~~~~~~~~~~~~~~~~~"
	exit 1
fi

echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"

rm diff head1 head2 out
