#!/bin/sh
timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
timeout 2 ./tcp2run -n 2 -s 2 ../../examples/jac1d 100 50 -10 > test-jac1d-resize-2-2.out
cmp test-jac1d-resize-2-2.out "$(dirname -- "${0}")/test-jac1d-resize-2-2.expected"
