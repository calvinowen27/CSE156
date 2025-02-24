#!/usr/bin/env bash

echo "
!!! RUNNING SERVERS !!!
"

servers=()

function kill_servers()
{
	echo "killing servers"

	for s in ${servers[@]}; do
		kill -9 $s
		wait $s &>/dev/null
	done

	echo "all servers killed"
	exit
}

trap kill_servers SIGINT

for i in $(seq 0 $(($1-1)))
do
	port=$((9090+i))

	out_path="out/server$i""_out/"
	./bin/myserver $port 15 $out_path &
	pid=$!
	servers+=($pid)
done

while true
do
	sleep 1
done
