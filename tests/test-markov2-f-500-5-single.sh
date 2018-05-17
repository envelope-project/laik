#!/bin/sh
LAIK_BACKEND=single ../examples/markov2 -f 500 5 > test-markov2-f-500-5.out
cmp test-markov2-f-500-5.out "$(dirname -- "${0}")/test-markov2-f-500-5.expected"
