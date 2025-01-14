#!/usr/bin/env bash

echo "
!!! RUNNING TEST_CHUNKED !!!
"

if [ -f output.dat ]; then
	rm output.dat
fi

./bin/myweb www.worldslongestwebsite.com 217.160.0.249:80/ > out 2> error1
echo "'Transfer-Encoding: chunked' header field is not supported for this lab." > error2
diff error1 error2 > diff

if [ -s diff ] || [ -f output.dat ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE: www.worldslongestwebsite.com
~~~~~~~~~~~~~~~~~~~~~~~"
else
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"
fi

rm error1 error2 diff out
