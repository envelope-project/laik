#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/vsum2 100 | LC_ALL='C' sort > test-vsum2-4.out
cmp test-vsum2-4.out "$(dirname -- "${0}")/test-vsum2-4.expected"
