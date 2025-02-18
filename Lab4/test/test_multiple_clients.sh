#!/usr/bin/env bash

echo "
!!! RUNNING TEST_MULTIPLE_CLIENTS !!!
"

./bin/myserver 9090 0 &
server_pid=$!

./bin/myclient 127.0.0.1 9090 8192 10 test_files/large_ascii.txt out/large_ascii_out.txt > client1out 2> client1err &
client1_pid=$!

./bin/myclient 127.0.0.1 9090 8192 25 test_files/large_binary.dat out/large_binary_out.dat > client2out 2> client2err &
client2_pid=$!

./bin/myclient 127.0.0.1 9090 64 5 test_files/small_ascii.txt out/small_ascii_out.txt > client3out 2> client3err

wait $client1_pid
wait $client2_pid

diff test_files/large_ascii.txt out/large_ascii_out.txt > diff1
diff test_files/large_binary.dat out/large_binary_out.dat > diff2
diff test_files/small_ascii.txt out/small_ascii_out.txt > diff3

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

if [ -s diff1 ] || [ -s diff2 ] || [ -s diff3 ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: at least one client failed to replicate file
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

# rm -rf out/ diff1 diff2 diff3
