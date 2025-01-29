#!/usr/bin/env bash

echo "
RUNNING TESTS
"

make clean
make

./test/test_ascii.sh
./test/test_multiple_users.sh
./test/test_bad_ip.sh
./test/test_server_crash.sh
./test/test_ooo_server.sh
