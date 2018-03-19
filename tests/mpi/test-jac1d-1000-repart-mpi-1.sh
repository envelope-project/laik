#!/bin/sh
LAIK_BACKEND=mpi mpiexec -n 1 ../../examples/jac1d 1000 50 10 > test-jac1d-1000-repart-mpi-1.out
cmp test-jac1d-1000-repart-mpi-1.out "$(dirname -- "${0}")/../test-jac1d-1000-repart.expected"
