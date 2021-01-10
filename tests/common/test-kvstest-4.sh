#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../src/kvstest > test-kvstest-4.out
cmp test-kvstest-4.out "$(dirname -- "${0}")/test-kvstest-4.expected"
