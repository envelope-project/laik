#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/spmv 4000 > test-spmv-1.out
cmp test-spmv-1.out "$(dirname -- "${0}")/test-spmv-1.expected"
