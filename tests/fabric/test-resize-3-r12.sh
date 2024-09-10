#!/bin/sh
timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
timeout 2 ./launcher -n 3 -r L[12] ../../examples/resize 1 | LC_ALL='C' sort > test-resize-3-r12.out
cmp test-resize-3-r12.out "$(dirname -- "${0}")/test-resize-3-r12.expected"
