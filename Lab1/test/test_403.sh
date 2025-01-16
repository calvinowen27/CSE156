#!/usr/bin/env bash

./bin/myweb www.neverssl.com 34.223.124.45:80/index.html

echo "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">
<html><head>
<title>403 Forbidden</title>
</head><body>
<h1>Forbidden</h1>
<p>You don't have permission to access this resource.</p>
</body></html>" > testout

diff output.dat testout > diff

if [ -s diff ]; then
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST FAILURE
~~~~~~~~~~~~~~~~~~~~~~~"
	exit 1
else
	echo "~~~~~~~~~~~~~~~~~~~~~~~
	TEST SUCCESS
~~~~~~~~~~~~~~~~~~~~~~~"
fi

rm testout