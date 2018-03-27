#!/bin/sh
LAIK_BACKEND=mpi mpiexec -n 4 ../../examples/vsum2 | LC_ALL='C' sort > test-vsum2-mpi-4.out
cmp test-vsum2-mpi-4.out "$(dirname -- "${0}")/test-vsum2.expected"
