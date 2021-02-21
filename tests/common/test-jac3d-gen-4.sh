#!/bin/sh
LAIK_LAYOUT_GENERIC=1 ${LAUNCHER-./launcher} -n 4 ../../examples/jac3d -s 100 10 > test-jac3d-gen-4.out
cmp test-jac3d-gen-4.out "$(dirname -- "${0}")/test-jac3d-4.expected"
