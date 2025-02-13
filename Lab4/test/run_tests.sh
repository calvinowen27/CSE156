#!/usr/bin/env bash

echo "
==========================
!!! RUNNING TESTS !!!
==========================

"

make clean
make

./test/test_bad_inputs.sh
./test/test_normal.sh
./test/test_high_drops.sh
./test/test_multiple_clients.sh
./test/test_server_crash.sh
