#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/spmv2 -r 10 3000 > test-spmv2r-1.out
cmp test-spmv2r-1.out "$(dirname -- "${0}")/test-spmv2-1.expected"
