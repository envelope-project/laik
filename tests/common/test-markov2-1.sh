#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/markov2 40 4 > test-markov2-1.out
cmp test-markov2-1.out "$(dirname -- "${0}")/test-markov2.expected"
