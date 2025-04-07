#!/bin/sh
timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
export UCP_COMMANDS_FILE=/home/ge96hoy2/laik/tests/ucp/test-jac1d-resize-4-r12-commands.txt
timeout 5 ./ucprun.sh --strict-node 0 -n 4 ../../examples/jac1d 100 50 -10 > test-jac1d-resize-4-r12.out
cmp test-jac1d-resize-4-r12.out "$(dirname -- "${0}")/test-jac1d-resize-4-r12.expected"
