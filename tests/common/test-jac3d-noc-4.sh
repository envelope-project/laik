#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/jac3d -s -n 100 10 > test-jac3d-noc-4.out
cmp test-jac3d-noc-4.out "$(dirname -- "${0}")/test-jac3d-noc-4.expected"
