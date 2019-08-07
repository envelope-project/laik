module load openmpi
make clean
make TARGET=REPARTITIONING -j
echo "running REPARTITIONING"
mpirun -x OMP_NUM_THREADS=1 --oversubscribe -host i10se1:13,i10se4:14 ./lulesh2.0 -s 30 -repart 0 > repart.log

make clean
make TARGET=PERFORMANCE -j
echo "running PERFORMANCE"
mpirun -x OMP_NUM_THREADS=1 --oversubscribe -host i10se1:13,i10se4:14 ./lulesh2.0 -s 30 -repart 0 > perf.log
