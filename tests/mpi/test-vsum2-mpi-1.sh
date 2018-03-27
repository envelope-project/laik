#!/bin/sh
LAIK_BACKEND=mpi mpiexec -n 1 ../../examples/vsum2 > test-vsum2-mpi-1.out
cmp test-vsum2-mpi-1.out "$(dirname -- "${0}")/../test-vsum.expected"
