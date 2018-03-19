#!/bin/sh
LAIK_BACKEND=mpi mpiexec -n 1 ../../examples/jac2d -s 1000 > test-jac2d-1000-mpi-1.out
cmp test-jac2d-1000-mpi-1.out "$(dirname -- "${0}")/../test-jac2d-1000.expected"
