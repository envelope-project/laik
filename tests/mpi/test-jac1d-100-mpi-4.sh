#!/bin/sh
LAIK_BACKEND=mpi mpiexec -n 4 ../../examples/jac1d 100 > test-jac1d-100-mpi-4.out
cmp test-jac1d-100-mpi-4.out "$(dirname -- "${0}")/test-jac1d-100.expected"
