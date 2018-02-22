#!/bin/sh
LAIK_BACKEND=mpi mpirun -np 4 ../../examples/spmv2 -r 10 3000 | sort > test-spmv2r-mpi-4.out
cmp test-spmv2r-mpi-4.out "$(dirname -- "${0}")/test-spmv2.expected"
