#!/bin/sh

export OMP_NUM_THREADS=1
EXECUTABLE="./lulesh"
mpirun --oversubscribe -n 8 "$EXECUTABLE" --failureCheckFrequency 5 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --plannedFailure 1 32 -s 10 -i 50 -p -repart 1 > test-lulesh-ft.out
cmp test-lulesh-ft.out "$(dirname -- "${0}")/test-lulesh-ft.expected"
