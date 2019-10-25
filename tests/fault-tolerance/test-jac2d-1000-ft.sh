#!/bin/sh

# $1 should contain the source, $2 the binary directory
echo "$1/checkpoint-jac2d-recovery, $2"
mpirun --oversubscribe -n 8 "$2/checkpoint-jac2d-recovery" --failureCheckFrequency 10 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --plannedFailure 1 15 --plannedFailure 2 21  --plannedFailure 3 32 --plannedFailure 5 33 -s 1000 > test-jac2d-1000-ft.out
diff -y test-jac2d-1000-ft.out "$1/test-jac2d-1000-ft.expected"
RESULT=$?
echo "Result: $RESULT"
exit $RESULT