#!/bin/sh
LAIK_BACKEND=mpi mpirun -np 4 ../../examples/jac3d -s 100 > test-jac3d-100-mpi-4.out
cmp test-jac3d-100-mpi-4.out $(dirname $0)/test-jac3d-100.expected
