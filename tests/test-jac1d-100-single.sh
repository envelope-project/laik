#!/bin/sh
LAIK_BACKEND=single ../examples/jac1d 100 > test-jac1d-100.out
cmp test-jac1d-100.out $(dirname $0)/test-jac1d-100.expected
