#!/bin/sh
LAIK_BACKEND=single ../examples/jac1d 1000 50 10 > test-jac1d-1000-repart.out
cmp test-jac1d-1000-repart.out $(dirname $0)/test-jac1d-1000-repart.expected
