#!/usr/bin/env bash

echo "
!!! RUNNING TEST_MULTIPLE_CLIENTS !!!
"

if [ -d out ]; then
	rm -rf out
fi

./test/run_servers.sh 4 &
servers=$!

./bin/myclient 3 test_files/test1.conf 1024 10 test_files/small_ascii.txt out/small_ascii_out.txt > client1out 2> client1err &
client1=$!

./bin/myclient 3 test_files/test2.conf 1024 10 test_files/large_binary.dat out/large_binary_out.dat > client2out 2> client2err &
client2=$!

wait $client1
wait $client2

diff test_files/small_ascii.txt out/server0_out/out/small_ascii_out.txt > diff00
diff test_files/small_ascii.txt out/server1_out/out/small_ascii_out.txt > diff01
diff test_files/small_ascii.txt out/server2_out/out/small_ascii_out.txt > diff02

diff test_files/large_binary.dat out/server1_out/out/large_binary_out.dat > diff11
diff test_files/large_binary.dat out/server2_out/out/large_binary_out.dat > diff12
diff test_files/large_binary.dat out/server3_out/out/large_binary_out.dat > diff13

if [ ! -d out/server0_out/out ] || [ ! -d out/server1_out/out ] || [ ! -d out/server2_out/out ] || [ ! -d out/server3_out/out ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: at least one out directory not created
~~~~~~~~~~~~~~~~~~~~~~~"
	kill -2 $servers
	wait $servers &>/dev/null

	kill -9 $client1
	wait $client1 &>/dev/null

	kill -9 $client2
	wait $client2 &>/dev/null

	exit 1
fi

if [ -s diff00 ] || [ -s diff01 ] || [ -s diff02 ] || [ -s diff11 ] || [ -s diff12 ] || [ -s diff13 ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: at least one client failed to replicate file
~~~~~~~~~~~~~~~~~~~~~~~"

	kill -2 $servers
	wait $servers &>/dev/null

	kill -9 $client1
	wait $client1 &>/dev/null

	kill -9 $client2
	wait $client2 &>/dev/null

	exit 1
fi

echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"

kill -2 $servers
wait $servers &>/dev/null

kill -9 $client1
wait $client1 &>/dev/null

kill -9 $client2
wait $client2 &>/dev/null

rm -rf out/server0_out/out out/server1_out/out out/server2_out/out out/server3_out/out diff00 diff01 diff02 diff11 diff12 diff13
