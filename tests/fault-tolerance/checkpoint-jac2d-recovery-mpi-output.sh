#!/bin/bash

export LAIK_BACKEND='mpi'
#export LAIK_TCP_CONFIG="./cmake-build-debug_wsl/tcp_config.txt"

mpirun -n 4 'bash -c ./cmake-build-debug_wsl/tests/fault-tolerance/checkpoint-jac2d-recovery 1>process_${OMPI_COMM_WORLD_RANK}.txt 2>&1'
