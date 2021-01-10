#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../src/spacestest | LC_ALL='C' sort > test-spaces-4.out
cmp test-spaces-4.out "$(dirname -- "${0}")/test-spaces-4.expected"
