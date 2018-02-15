#!/bin/sh
LAIK_BACKEND=single ../examples/vsum3 > test-vsum3-single.out
cmp test-vsum3-single.out $(dirname $0)/test-vsum.expected
