#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/jac3d -r -i 10 -s 100 10 > test-jac3dri-4.out
cmp test-jac3dri-4.out "$(dirname -- "${0}")/test-jac3di-4.expected"
