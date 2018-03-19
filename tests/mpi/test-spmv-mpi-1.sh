#!/bin/sh
LAIK_BACKEND=mpi mpiexec -n 1 ../../examples/spmv 4000 > test-spmv-mpi-1.out
cmp test-spmv-mpi-1.out "$(dirname -- "${0}")/../test-spmv.expected"
