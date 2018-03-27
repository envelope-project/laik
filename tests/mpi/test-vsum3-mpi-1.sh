#!/bin/sh
LAIK_BACKEND=mpi mpiexec -n 1 ../../examples/vsum3 > test-vsum3-mpi-1.out
cmp test-vsum3-mpi-1.out "$(dirname -- "${0}")/../test-vsum.expected"
