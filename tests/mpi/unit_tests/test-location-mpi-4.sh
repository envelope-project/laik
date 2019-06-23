#!/bin/sh
# called from parent directory
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ../src/locationtest > test-location-mpi-4.out
