#!/bin/sh
timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
timeout 2 ./tcp2run -n 3 -r L1 ../../examples/resize 1 | LC_ALL='C' sort > test-resize-3-r1.out
./mycmp test-resize-3-r1.out "$(dirname -- "${0}")/test-resize-3-r1.expected"
