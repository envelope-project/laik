#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/jac3d -e -s 100 10 > test-jac3de-4.out
cmp test-jac3de-4.out "$(dirname -- "${0}")/test-jac3d-4.expected"
