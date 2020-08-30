#!/bin/sh
LAIK_BACKEND=mpi LAIK_DO_GENERIC_COPY=1 ${MPIEXEC-mpiexec} -n 4 ../../examples/jac2d -s 1000 > test-jac2d-gencopy-1000-mpi-4.out
cmp test-jac2d-gencopy-1000-mpi-4.out "$(dirname -- "${0}")/test-jac2d-1000.expected"
