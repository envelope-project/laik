#!/bin/sh
LAIK_LAYOUT_GENERIC=1 ${LAUNCHER-./launcher} -n 4 ../../examples/jac2d -s 100 > test-jac2d-gen-4.out
cmp test-jac2d-gen-4.out "$(dirname -- "${0}")/test-jac2d-4.expected"
