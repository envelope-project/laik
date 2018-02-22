#!/bin/sh
LAIK_BACKEND=mpi mpirun -np 1 ../../examples/jac3d -s 100 > test-jac3d-100-mpi-1.out
cmp test-jac3d-100-mpi-1.out "$(dirname -- "${0}")/../test-jac3d-100.expected"
