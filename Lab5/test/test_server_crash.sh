#!/usr/bin/env bash

echo "
!!! RUNNING TEST_SERVER_CRASH !!!
"

mkdir -p out

./bin/myserver 9090 3 &
server_pid=$!

./bin/myclient 127.0.0.1 9090 1024 20 test_files/large_binary.dat out/large_binary_out.dat &
client_pid=$!

kill -9 $server_pid
wait $client_pid

if [ $? -eq 4 ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"
else
echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: clent did not terminate when server crashed
~~~~~~~~~~~~~~~~~~~~~~~"
fi

wait $server_pid &>/dev/null

rm -rf out/
