#!/bin/sh
LAIK_BACKEND=mpi mpirun -np 4 ../../examples/vsum3 | sort > test-vsum3-mpi-4.out
cmp test-vsum3-mpi-4.out $(dirname $0)/test-vsum.expected
