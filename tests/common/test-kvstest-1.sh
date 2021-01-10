#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../src/kvstest > test-kvstest-1.out
cmp test-kvstest-1.out "$(dirname -- "${0}")/test-kvstest-1.expected"
