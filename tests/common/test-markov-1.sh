#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/markov 40 4 > test-markov-1.out
cmp test-markov-1.out "$(dirname -- "${0}")/test-markov.expected"
