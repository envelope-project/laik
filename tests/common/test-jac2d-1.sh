#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/jac2d -s 100 > test-jac2d-1.out
cmp test-jac2d-1.out "$(dirname -- "${0}")/test-jac2d-1.expected"
