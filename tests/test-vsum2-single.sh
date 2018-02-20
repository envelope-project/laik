#!/bin/sh
LAIK_BACKEND=single ../examples/vsum2 > test-vsum2-single.out
cmp test-vsum2-single.out "$(dirname -- "${0}")/test-vsum.expected"
