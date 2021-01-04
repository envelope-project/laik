#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/vsum 100 > test-vsum-1.out
cmp test-vsum-1.out "$(dirname -- "${0}")/test-vsum-1.expected"
