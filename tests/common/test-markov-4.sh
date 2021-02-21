#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/markov 40 4 > test-markov-4.out
cmp test-markov-4.out "$(dirname -- "${0}")/test-markov.expected"
