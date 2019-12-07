#!/bin/sh

mpirun --oversubscribe -n 8 "../../tests/fault-tolerance/checkpoint-jac2d-recovery" --failureCheckFrequency 10 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --plannedFailure 1 15 --plannedFailure 2 21  --plannedFailure 3 32 --plannedFailure 5 33 -s 1000 > test-jac2d-1000-ft.out
cmp test-jac2d-1000-ft.out "$(dirname -- "${0}")/test-jac2d-1000-ft.expected"