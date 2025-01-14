#!/usr/bin/env bash

echo "
RUNNING TESTS
"

make clean
make

./test/test_bad_ip.sh
./test/test_bad_port.sh
./test/test_example.sh
./test/test_header.sh
./test/test_chunked.sh
