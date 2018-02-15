#!/bin/sh
LAIK_BACKEND=single ../examples/propagation2d 10 10 > test-propagation2d-10.out
cmp test-propagation2d-10.out $(dirname $0)/test-propagation2d-10.expected
