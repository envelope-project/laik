#!/bin/sh
timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
export UCP_COMMANDS_FILE=/home/ge96hoy2/laik/tests/ucp/test-resize-3-r1-commands.txt
timeout 2 ./ucprun.sh --strict-node 0 -n 3 ../../examples/resize 1 | LC_ALL='C' sort > test-resize-3-r1.out
./mycmp test-resize-3-r1.out "$(dirname -- "${0}")/test-resize-3-r1.expected"
