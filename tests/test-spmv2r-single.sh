#!/bin/sh
LAIK_BACKEND=single ../examples/spmv2 -r 10 3000 > test-spmv2r-single.out
cmp test-spmv2r-single.out $(dirname $0)/test-spmv2.expected
