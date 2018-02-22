#!/bin/sh
LAIK_BACKEND=mpi mpirun -np 4 ../../examples/vsum | sort > test-vsum-mpi-4.out
cmp test-vsum-mpi-4.out "$(dirname -- "${0}")/test-vsum.expected"
