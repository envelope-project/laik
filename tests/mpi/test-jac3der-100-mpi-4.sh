#!/bin/sh
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ../../examples/jac3d -e -r -s 100 > test-jac3der-100-mpi-4.out
cmp test-jac3der-100-mpi-4.out "$(dirname -- "${0}")/test-jac3d-100.expected"
