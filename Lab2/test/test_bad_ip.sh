#!/usr/bin/env bash

echo "
!!! RUNNING TEST_BAD_IP !!!
"

if [ -f out/small_ascii_out.txt ]; then
	rm out/small_ascii_out.txt
fi

./bin/myserver 9090 &
pid=$!

./bin/myclient 0.-1.0.0 9090 1024 test_files/small_ascii.txt out/small_ascii_out.txt

if [ ! -s out/small_ascii_out.txt ]; then
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