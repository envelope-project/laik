#!/bin/sh
LAIK_LOG=1:0 LAIK_BACKEND=single ../examples/vsum > test-vsum.out 2>/dev/null
cmp test-vsum.out $(dirname $0)/test-vsum.expected
