#!/bin/sh
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ../../examples/vsum | LC_ALL='C' sort > test-vsum-mpi-4.out
cmp test-vsum-mpi-4.out "$(dirname -- "${0}")/test-vsum.expected"
