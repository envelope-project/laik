#!/bin/bash

export LAIK_BACKEND='tcp'
export LAIK_TCP_CONFIG="./cmake-build-debug_wsl/tcp_config.txt"


if [ -z $1 ]
then
    num=1
else
    num=$1
fi

if [ -z $2 ]
then
    tNum=2
else
    tNum=$2
fi

echo -n "[general]
receive_timeout = 0.1
receive_attempts = 3

[addresses]
" > ./cmake-build-debug_wsl/tcp_config.txt

for i in `seq 1 $tNum`
do
    echo "rank$i = localhost $(expr 10000 + $i)" >> ./cmake-build-debug_wsl/tcp_config.txt
done

echo "Launching $num processes"
for i in `seq 1 $num`
do
    ./cmake-build-debug_wsl/tests/fault-tolerance/checkpoint-jac2d-recovery &
done

wait
