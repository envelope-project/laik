#!/bin/sh
# called from parent directory
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ../src/spacestest > test-spaces-mpi-4.out
