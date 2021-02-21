#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/propagation2d 10 10 > test-propagation2d-1.out
cmp test-propagation2d-1.out "$(dirname -- "${0}")/test-propagation2d.expected"
