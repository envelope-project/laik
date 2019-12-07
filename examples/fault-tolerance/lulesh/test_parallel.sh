echo "testing 8, s 2, no repartitioning"
OMP_NUM_THREADS=1 mpirun -np 8 --oversubscribe ./lulesh2.0 -q -s 2 -repart 0
echo "testing 8, s 5, no repartitioning"
OMP_NUM_THREADS=1 mpirun -np 8 --oversubscribe ./lulesh2.0 -q -s 5 -repart 0
echo "testing 27, s 2, no repartitioning"
OMP_NUM_THREADS=1 mpirun -np 27 --oversubscribe ./lulesh2.0 -q -s 2 -repart 0
echo "testing 27, s 5, no repartitioning"
OMP_NUM_THREADS=1 mpirun -np 27 --oversubscribe ./lulesh2.0 -q -s 5 -repart 0


