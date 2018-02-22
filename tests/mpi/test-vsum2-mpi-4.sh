#!/bin/sh
LAIK_BACKEND=mpi mpirun -np 4 ../../examples/vsum2 | sort > test-vsum2-mpi-4.out
cmp test-vsum2-mpi-4.out "$(dirname -- "${0}")/test-vsum2.expected"
