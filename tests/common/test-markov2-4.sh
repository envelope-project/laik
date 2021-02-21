#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../../examples/markov2 40 4 > test-markov2-4.out
cmp test-markov2-4.out "$(dirname -- "${0}")/test-markov2.expected"
