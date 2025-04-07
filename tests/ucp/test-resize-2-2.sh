#!/bin/sh
timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
export UCP_COMMANDS_FILE=/home/ge96hoy2/laik/tests/ucp/test-resize-2-2-commands.txt
timeout 2 ./ucprun.sh --strict-node 0 -n 2 -s 2 ../../examples/resize 1 | LC_ALL='C' sort > test-resize-2-2.out
./mycmp test-resize-2-2.out "$(dirname -- "${0}")/test-resize-2-2.expected"
