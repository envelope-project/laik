#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/vsum 100 | LC_ALL='C' sort > test-vsum-4.out
cmp test-vsum-4.out "$(dirname -- "${0}")/test-vsum-4.expected"
