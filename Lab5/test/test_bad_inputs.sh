#!/usr/bin/env bash

echo "
!!! RUNNING TEST_BAD_INPUTS !!!
"

if [ -d out/ ]; then
	rm -rf out/
fi

./bin/myserver 9090 0 &
pid=$!

./bin/myclient 127.0.0.1 9090 14 0 test_files/small_ascii.txt out/small_ascii_out.txt

if [ -s out/small_ascii_out.txt ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE
~~~~~~~~~~~~~~~~~~~~~~~"
else
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"
fi

kill -9 $pid
wait $pid &>/dev/null
exit 1