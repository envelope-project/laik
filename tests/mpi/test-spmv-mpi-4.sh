#!/bin/sh
LAIK_BACKEND=mpi mpirun -np 4 ../../examples/spmv 4000 | LC_ALL='C' sort > test-spmv-mpi-4.out
cmp test-spmv-mpi-4.out "$(dirname -- "${0}")/test-spmv.expected"
