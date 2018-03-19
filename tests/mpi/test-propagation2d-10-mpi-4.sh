#!/bin/sh
LAIK_BACKEND=mpi mpiexec -n 4 ../../examples/propagation2d 10 10 > test-propagation2d-10-mpi-4.out
cmp test-propagation2d-10-mpi-4.out "$(dirname -- "${0}")/test-propagation2d-10.expected"
