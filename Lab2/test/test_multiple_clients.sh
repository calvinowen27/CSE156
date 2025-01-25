#!/usr/bin/env bash

echo "
!!! RUNNING TEST_MULTIPLE_CLIENTS !!!
"

./bin/myserver 9090 &
server_pid=$!

./bin/myclient 127.0.0.1 9090 4096 test_files/large_ascii.txt out/large_ascii_out1.txt &
client1_pid=$!

./bin/myclient 127.0.0.1 9090 4096 test_files/large_ascii.txt out/large_ascii_out2.txt &
client2_pid=$!

wait $client1_pid
wait $client2_pid

diff test_files/large_ascii.txt out/large_ascii_out1.txt > diff1
diff test_files/large_ascii.txt out/large_ascii.out2.txt > diff2

if [ ! -d out ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: out directory not created
~~~~~~~~~~~~~~~~~~~~~~~"
	kill -9 $server_pid
	wait $server_pid &>/dev/null

	kill -9 $client1_pid
	wait $client1_pid &>/dev/null

	kill -9 $client2_pid
	wait $client2_pid &>/dev/null
	exit 1
fi

if [ -s diff1 ] && [ -s diff2 ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: client didn't correctly replicate file
~~~~~~~~~~~~~~~~~~~~~~~"
	kill -9 $server_pid
	wait $server_pid &>/dev/null

	kill -9 $client1_pid
	wait $client1_pid &>/dev/null

	kill -9 $client2_pid
	wait $client2_pid &>/dev/null
	exit 1
fi

echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"

kill -9 $server_pid
wait $server_pid &>/dev/null

rm -rf out/ diff diff1 diff2
