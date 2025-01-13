#!/usr/bin/env bash

make clean
make
./bin/myweb www.example.com 93.184.216.24:80/index.html
curl 93.184.216.34:80/index.html -H "Host: www.example.com" -o output1.dat
diff output.dat output1.dat > diff.dat
if [ -s diff.dat ]; then
	echo "success"
else
	echo "failure"
fi
