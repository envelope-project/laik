#!/bin/sh
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 1 ../../examples/jac3d -r -s 100 > test-jac3dr-100-mpi-1.out
cmp test-jac3dr-100-mpi-1.out "$(dirname -- "${0}")/../test-jac3d-100.expected"
