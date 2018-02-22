#!/bin/sh
LAIK_BACKEND=mpi mpirun -np 1 ../../examples/vsum > test-vsum-mpi-1.out
cmp test-vsum-mpi-1.out "$(dirname -- "${0}")/../test-vsum.expected"
