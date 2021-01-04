#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/jac1d 100 50 10 > test-jac1d-repart-1.out
cmp test-jac1d-repart-1.out "$(dirname -- "${0}")/test-jac1d-repart-1.expected"
