#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/propagation2d 10 10 > test-propagation2d-4.out
cmp test-propagation2d-4.out "$(dirname -- "${0}")/test-propagation2d.expected"
