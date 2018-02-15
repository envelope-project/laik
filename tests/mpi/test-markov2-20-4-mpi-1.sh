#!/bin/sh
LAIK_BACKEND=mpi mpirun -np 1 ../../examples/markov2 20 4 > test-markov2-20-4-mpi-1.out
cmp test-markov2-20-4-mpi-1.out $(dirname $0)/../test-markov2-20-4.expected
