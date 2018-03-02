#!/bin/sh
LAIK_BACKEND=single ../examples/jac3d -r -s 100 > test-jac3dr-100.out
cmp test-jac3dr-100.out "$(dirname -- "${0}")/test-jac3d-100.expected"
