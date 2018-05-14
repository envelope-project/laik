#!/bin/sh
# test shrinking in spmv2
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ../../examples/spmv2 -s 2 10 3000 | LC_ALL='C' sort > test-spmv2-shrink-mpi-4.out
cmp test-spmv2-shrink-mpi-4.out "$(dirname -- "${0}")/test-spmv2.expected"
