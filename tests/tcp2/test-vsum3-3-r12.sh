#!/bin/sh
timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
timeout 2 ${LAUNCHER-./launcher} -n 3 -r L[12] ../../examples/vsum3 1 | LC_ALL='C' sort > test-vsum3-3-r12.out
cmp test-vsum3-3-r12.out "$(dirname -- "${0}")/test-vsum3-3-r12.expected"
