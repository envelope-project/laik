#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/vsum2 100 > test-vsum2-1.out
cmp test-vsum2-1.out "$(dirname -- "${0}")/test-vsum-1.expected"
