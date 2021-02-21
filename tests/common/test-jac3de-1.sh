#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/jac3d -e -s 100 10 > test-jac3de-1.out
cmp test-jac3de-1.out "$(dirname -- "${0}")/test-jac3d-1.expected"
