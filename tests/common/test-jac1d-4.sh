#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/jac1d 100 > test-jac1d-4.out
cmp test-jac1d-4.out "$(dirname -- "${0}")/test-jac1d-4.expected"
