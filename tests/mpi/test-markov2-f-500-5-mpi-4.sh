#!/bin/sh
LAIK_BACKEND=mpi ${MPIEXEC-mpiexec} -n 4 ../../examples/markov2 -f 500 5 > test-markov2-f-500-5-mpi-4.out
cmp test-markov2-f-500-5-mpi-4.out "$(dirname -- "${0}")/../test-markov2-f-500-5.expected"
