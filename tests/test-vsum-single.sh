#!/bin/sh
LAIK_BACKEND=single ../examples/vsum > test-vsum-single.out
cmp test-vsum-single.out "$(dirname -- "${0}")/test-vsum.expected"
