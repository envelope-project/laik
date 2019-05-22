#!/bin/sh
# test reservation with task without slice (grid partitioning among 3 of 4 MPI tasks)
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ../../examples/jac3d -r -g -x 3 -s 100 > test-jac3d-rgx3-100-mpi-4.out
cmp test-jac3d-rgx3-100-mpi-4.out "$(dirname -- "${0}")/test-jac3d-gx3-100.expected"
