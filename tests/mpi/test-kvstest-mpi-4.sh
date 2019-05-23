#!/bin/sh
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ../src/kvstest > test-kvstest-mpi-4.out
cmp test-kvstest-mpi-4.out "$(dirname -- "${0}")/test-kvstest-mpi-4.expected"
