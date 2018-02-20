#!/bin/sh
LAIK_BACKEND=single ../examples/markov 20 4 > test-markov-20-4.out
cmp test-markov-20-4.out "$(dirname -- "${0}")/test-markov-20-4.expected"
