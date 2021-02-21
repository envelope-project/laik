#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/propagation2d -o 10 10 > test-propagation2do-4.out
cmp test-propagation2do-4.out "$(dirname -- "${0}")/test-propagation2d.expected"
