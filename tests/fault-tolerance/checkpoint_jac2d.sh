#!/bin/bash

IFS='_'

export LAIK_BACKEND='tcp'
export LAIK_TCP_CONFIG="./cmake-build-debug_wsl/tcp_config.txt"

pwd

for i in 1 2 3 4; do
    ./cmake-build-debug_wsl/tests/fault-tolerance/checkpoint-jac2d &
done

wait
