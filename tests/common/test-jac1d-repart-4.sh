#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/jac1d 100 50 10 > test-jac1d-repart-4.out
cmp test-jac1d-repart-4.out "$(dirname -- "${0}")/test-jac1d-repart-4.expected"
