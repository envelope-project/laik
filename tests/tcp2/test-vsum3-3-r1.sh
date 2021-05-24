#!/bin/sh
timeout 2 ./tcp2run -n 3 -r L1 ../../examples/vsum3 1 | LC_ALL='C' sort > test-vsum3-3-r1.out
cmp test-vsum3-3-r1.out "$(dirname -- "${0}")/test-vsum3-3-r1.expected"
