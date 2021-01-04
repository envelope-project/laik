#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/spmv 4000 | LC_ALL='C' sort > test-spmv-4.out
cmp test-spmv-4.out "$(dirname -- "${0}")/test-spmv-4.expected"
