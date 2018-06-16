#!/bin/sh
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ../../examples/jac3d -a -s 100 > test-jac3da-100-mpi-4.out
cmp test-jac3da-100-mpi-4.out "$(dirname -- "${0}")/test-jac3d-100.expected"
