#!/bin/sh
LAIK_BACKEND=mpi mpirun -np 1 ../../examples/spmv2 10 3000 > test-spmv2-mpi-1.out
cmp test-spmv2-mpi-1.out $(dirname $0)/../test-spmv2.expected
