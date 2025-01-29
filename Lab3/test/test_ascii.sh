#!/usr/bin/env bash

echo "
!!! RUNNING TEST_ASCII !!!
"

./bin/myserver 9090 &
pid=$!

./bin/myclient 127.0.0.1 9090 1024 test_files/small_ascii.txt out/small_ascii_out.txt
diff test_files/small_ascii.txt out/small_ascii_out.txt > diff

if [ ! -d out ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: out directory not created
~~~~~~~~~~~~~~~~~~~~~~~"
	kill -9 $pid
	wait $pid &>/dev/null
	exit 1
fi

if [ -s diff ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: small_ascii.txt
~~~~~~~~~~~~~~~~~~~~~~~"
	kill -9 $pid
	wait $pid &>/dev/null
	exit 1
fi

./bin/myclient 127.0.0.1 9090 4096 test_files/large_ascii.txt out/large_ascii_out.txt
diff test_files/large_ascii.txt out/large_ascii_out.txt > diff

if [ -s diff ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: large_ascii.txt
~~~~~~~~~~~~~~~~~~~~~~~"
	kill -9 $pid
	wait $pid &>/dev/null
	exit 1
fi

echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"

kill -9 $pid
wait $pid &>/dev/null

rm -rf out/ diff
