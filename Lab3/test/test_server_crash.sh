#!/usr/bin/env bash

echo "
!!! RUNNING TEST_SERVER_CRASH !!!
"

make crashingserver

mkdir -p out

./bin/crashingserver 9090 &
server_pid=$!

./bin/myclient 127.0.0.1 9090 5 test_files/small_ascii.txt out/small_ascii_out.txt > out/out 2> out/err &
client_pid=$!

wait $client_pid

if grep -q "Cannot detect server." out/err; then
  echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"
else
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: client didn't respond correctly to server crash
~~~~~~~~~~~~~~~~~~~~~~~"
fi

kill -9 $server_pid
wait $server_pid &>/dev/null

rm -rf out/
