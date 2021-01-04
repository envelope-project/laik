#!/bin/sh
# test with no-corners halo partitioner
${LAUNCHER-./launcher} -n 4 ../../examples/jac2d -s -n 100 > test-jac2d-noc-4.out
cmp test-jac2d-noc-4.out "$(dirname -- "${0}")/test-jac2d-noc-4.expected"
