#!/bin/sh
OMP_NUM_THREADS=1 ${LAUNCHER-./launcher} -n 4 ../../examples/spmv2 10 3000 | LC_ALL='C' sort > test-spmv2-4.out
cmp test-spmv2-4.out "$(dirname -- "${0}")/test-spmv2-4.expected"
