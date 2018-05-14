#!/bin/sh
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 1 ../../examples/markov 20 4 > test-markov-20-4-mpi-1.out
cmp test-markov-20-4-mpi-1.out "$(dirname -- "${0}")/../test-markov-20-4.expected"
