#!/bin/sh

# $1 should contain the source, $2 the binary directory
echo "$1/lulesh/lulesh, $2"
export OMP_NUM_THREADS=1
mpirun --oversubscribe -n 8 "$2/lulesh/lulesh" --failureCheckFrequency 5 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 -s 10 -i 50 -p -repart 1 > test-lulesh-ft.out
diff -y test-lulesh-ft.out "$1/test-lulesh-ft.expected"
RESULT=$?
echo "Result: $RESULT"
exit $RESULT