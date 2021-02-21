#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/jac3d -a -r -s 100 10 > test-jac3dar-4.out
cmp test-jac3dar-4.out "$(dirname -- "${0}")/test-jac3d-4.expected"
