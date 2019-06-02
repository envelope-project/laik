#!/bin/sh
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 1 ../src/kvstest > test-kvstest-mpi-1.out
cmp test-kvstest-mpi-1.out "$(dirname -- "${0}")/../test-kvstest.expected"
