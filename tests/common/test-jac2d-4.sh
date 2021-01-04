#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/jac2d -s 100 > test-jac2d-4.out
cmp test-jac2d-4.out "$(dirname -- "${0}")/test-jac2d-4.expected"
