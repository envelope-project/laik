#!/bin/sh
echo "Core test"
#LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ./core || exit
echo "Checkpoint test"
LAIK_BACKEND=mpi mpirun -n 4 ./checkpoint || exit