#!/bin/sh
timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
timeout 2 ./tcp2run -n 2 -s 2 ../../examples/vsum3 1 | LC_ALL='C' sort > test-vsum3-2-2.out
cmp test-vsum3-2-2.out "$(dirname -- "${0}")/test-vsum3-2-2.expected"
