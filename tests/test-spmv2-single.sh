#!/bin/sh
LAIK_BACKEND=single ../examples/spmv2 10 3000 > test-spmv2-single.out
cmp test-spmv2-single.out "$(dirname -- "${0}")/test-spmv2.expected"
