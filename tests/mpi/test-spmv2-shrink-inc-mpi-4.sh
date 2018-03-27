#!/bin/sh
# test shrinking with incremental partitioner
LAIK_BACKEND=mpi mpiexec -n 4 ../../examples/spmv2 -s 2 -i 10 3000 | LC_ALL='C' sort > test-spmv2-shrink-inc-mpi-4.out
cmp test-spmv2-shrink-inc-mpi-4.out "$(dirname -- "${0}")/test-spmv2.expected"
