#!/bin/sh
LAIK_BACKEND=single ../examples/markov2 20 4 > test-markov2-20-4.out
cmp test-markov2-20-4.out $(dirname $0)/test-markov2-20-4.expected
