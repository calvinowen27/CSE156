#!/usr/bin/env bash

echo "
!!! RUNNING TEST_HIGH_DROPS !!!
"

mkdir -p out

./bin/myserver 9090 25 &
server_pid=$!

./bin/myclient 127.0.0.1 9090 1024 20 test_files/large_ascii.txt out/large_ascii_out.txt

diff test_files/large_ascii.txt out/large_ascii_out.txt > diff

if [ ! -d out ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: out directory not created
~~~~~~~~~~~~~~~~~~~~~~~"
	kill -9 $server_pid
	wait $server_pid &>/dev/null
	exit 1
fi

if [ -s diff ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"
	kill -9 $server_pid
	wait $server_pid &>/dev/null
	exit
fi

echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: client did not terminate packet retransmission
~~~~~~~~~~~~~~~~~~~~~~~"

kill -9 $server_pid
wait $server_pid &>/dev/null

rm -rf out/
