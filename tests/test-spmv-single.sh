#!/bin/sh
LAIK_BACKEND=single ../examples/spmv 4000 > test-spmv-single.out
cmp test-spmv-single.out $(dirname $0)/test-spmv.expected
