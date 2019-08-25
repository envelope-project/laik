#!/bin/bash

export LAIK_BACKEND='mpi'
#export LAIK_TCP_CONFIG="./cmake-build-debug_wsl/tcp_config.txt"

if [ -z $1 ]
then
    num=1
else
    num=$1
fi

mpirun -n 4 ./cmake-build-debug_wsl/tests/fault-tolerance/checkpoint-jac2d-recovery
