#!/bin/sh
LAIK_BACKEND=single src/kvstest > test-kvstest-single.out
cmp test-kvstest-single.out "$(dirname -- "${0}")/test-kvstest.expected"
