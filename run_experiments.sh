#!/bin/bash
#SBATCH -p rpi # partition (queue)
###SBATCH -N 32 # number of nodes
#SBATCH -n 32 # number of tasks
###SBATCH --mem 100 # memory pool for all cores
#SBATCH -t 0-0:10 # time (D-HH:MM)
#SBATCH -o slurm.%N.%j.out # STDOUT
#SBATCH -e slurm.%N.%j.err # STDERR

export PATH="/u/home/bodev/lib/bin:$PATH"

# $1: Name, $2 Type, $3 Executable, $4 Executable_args, $5 run_number
run_experiment () {
	echo "Running experiment $1"
	export MPI_OPTIONS="--output-filename out --merge-stderr-to-stdout --stdin none"
#	export MPI_OPTIONS="$MPI_OPTIONS --oversubscribe"
	export TEST_NAME="$1"
	rm -r out/
	tests/fault-tolerance/launcher.sh "$2" "$5" "$5" release "$3" "$4"
	EXIT_CODE=$?
	grep -h -e "===" out/1/rank.*/stdout > "laik_experiments/data/experiment_$1_trace.csv"
	grep -h -e '!!!' out/1/rank.*/stdout > "laik_experiments/data/experiment_$1_header.csv"

	if [ $EXIT_CODE -ne 0 ]
	then
	  echo "Error: $EXIT_CODE"
	  exit $EXIT_CODE
  fi
}

#PREFIX="."
PREFIX=cmake-build-debug_wsl

# JAC2D Options [options] <side width> <maxiter> <repart>
JAC2D=$PREFIX/tests/fault-tolerance/checkpoint-jac2d-recovery
LULESH=$PREFIX/tests/fault-tolerance/lulesh/lulesh

if [ ! -f "$JAC2D" ] || [ ! -f "$LULESH" ]
then
  echo "MISSING Executable"
  exit 255
fi

EXECUTABLE="$JAC2D"
PROBLEM_SIZE=1024
MAX_ITER=50
num=4

run_experiment "mpi_jac2d_no_failure" "mpi" "$EXECUTABLE" "--checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $PROBLEM_SIZE $MAX_ITER -1" "$num"
run_experiment "mpi_jac2d_single_failure" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $PROBLEM_SIZE $MAX_ITER -1" "$num"
run_experiment "mpi_jac2d_no_checkpoint_single_failure" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --checkpointFrequency 0 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 --skipCheckpointRecovery $PROBLEM_SIZE $MAX_ITER -1" "$num"

run_experiment "mpi_jac2d_double_failure_sequential" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --plannedFailure 3 39 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $PROBLEM_SIZE $MAX_ITER -1" "$num"
run_experiment "mpi_jac2d_double_failure_parallel" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --plannedFailure 3 29 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $PROBLEM_SIZE $MAX_ITER -1" "$num"

EXECUTABLE="$LULESH"
LULESH_OPTIONS="-i 50 -s 8"
PROBLEM_SIZE=1024
MAX_ITER=50

num=8
run_experiment "mpi_lulesh_no_failure" "mpi" "$EXECUTABLE" "--checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $LULESH_OPTIONS" "$num"
run_experiment "mpi_lulesh_single_failure" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $LULESH_OPTIONS" "$num"
run_experiment "mpi_lulesh_no_checkpoint_single_failure" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --checkpointFrequency 0 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 --skipCheckpointRecovery $LULESH_OPTIONS" "$num"

num=27
run_experiment "mpi_lulesh_double_failure_sequential" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --plannedFailure 3 39 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $LULESH_OPTIONS" "$num"
run_experiment "mpi_lulesh_double_failure_parallel" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --plannedFailure 3 29 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $LULESH_OPTIONS" "$num"

EXECUTABLE="$JAC2D"

for num in 01 02 04 08 16 32
do
  run_experiment "mpi_strong_scale_$num" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 1024 50 -1" "$num"
done


for num in 01 02 04 08 16 32 64 128 258 512
do
        PROBLEM_SIZE="$(awk "BEGIN {printf \"%.0f\n\", sqrt(65536 * $num)}")"
        echo "Weak scaling test with problem size $PROBLEM_SIZE"
        run_experiment "mpi_weak_scale_$num" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $PROBLEM_SIZE 50 -1" "$num"
done
