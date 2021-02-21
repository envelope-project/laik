#!/bin/sh
# test reservation with task without slice (grid partitioning among 3 of 4 MPI tasks)
${LAUNCHER-./launcher} -n 4 ../../examples/jac3d -r -g -x 3 -s 100 10 > test-jac3d-rgx3-4.out
cmp test-jac3d-rgx3-4.out "$(dirname -- "${0}")/test-jac3d-gx3-4.expected"
