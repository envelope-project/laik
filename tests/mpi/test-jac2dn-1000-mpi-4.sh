#!/bin/sh
# test with no-corners halo partitioner
LAIK_BACKEND=mpi mpirun -np 4 ../../examples/jac2d -s -n 1000 > test-jac2dn-1000-mpi-4.out
cmp test-jac2dn-1000-mpi-4.out "$(dirname -- "${0}")/test-jac2dn-1000.expected"
