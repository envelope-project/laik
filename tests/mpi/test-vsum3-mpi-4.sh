#!/bin/sh
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ../../examples/vsum3 | LC_ALL='C' sort > test-vsum3-mpi-4.out
cmp test-vsum3-mpi-4.out "$(dirname -- "${0}")/test-vsum.expected"
