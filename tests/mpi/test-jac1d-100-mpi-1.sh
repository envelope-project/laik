#!/bin/sh
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 1 ../../examples/jac1d 100 > test-jac1d-100-mpi-1.out
cmp test-jac1d-100-mpi-1.out "$(dirname -- "${0}")/../test-jac1d-100.expected"
