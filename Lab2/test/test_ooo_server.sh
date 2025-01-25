#!/usr/bin/env bash

echo "
!!! RUNNING TEST_OOO_SERVER !!!
"

make oooserver

mkdir -p out

./bin/oooserver 9090 &
server_pid=$!

./bin/myclient 127.0.0.1 9090 1024 test_files/large_ascii.txt out/large_ascii_out.txt

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
	TEST FAILURE: client didn't correctly replicate file
~~~~~~~~~~~~~~~~~~~~~~~"
	kill -9 $server_pid
	wait $server_pid &>/dev/null
	exit 1
fi

echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"

kill -9 $server_pid
wait $server_pid &>/dev/null

rm -rf out/
