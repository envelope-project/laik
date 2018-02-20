#!/bin/sh
LAIK_BACKEND=single ../examples/jac2d -s 1000 > test-jac2d-1000-single.out
cmp test-jac2d-1000-single.out "$(dirname -- "${0}")/test-jac2d-1000.expected"
