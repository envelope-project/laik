#!/bin/sh
${LAUNCHER-./launcher} -n 1 ../../examples/markov2 -f 500 5 3 > test-markov2f-1.out
cmp test-markov2f-1.out "$(dirname -- "${0}")/test-markov2f.expected"
