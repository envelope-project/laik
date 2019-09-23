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
	grep -h -e "===" out/1/rank.*/stdout > "laik_experiments/data/experiment_$1_trace.csv"
	grep -h -e '!!!' out/1/rank.*/stdout > "laik_experiments/data/experiment_$1_header.csv"
}

PREFIX="."
#PREFIX=cmake-build-debug_wsl

# JAC2D Options [options] <side width> <maxiter> <repart>
JAC2D=$PREFIX/tests/fault-tolerance/checkpoint-jac2d-recovery

if [ ! -f "$JAC2D" ]
then
  echo "MISSING Executable"
  exit 255
fi

for num in 01 02 04 08 16 32
do
	run_experiment "mpi_strong_scale_$num" "mpi" "$JAC2D" "1024 50 -1 1 1" "$num"
done


for num in 01 02 04 08 16 32
do
        PROBLEM_SIZE="$(awk "BEGIN {printf \"%.0f\n\", sqrt(65536 * $num)}")"
        echo "Weak scaling test with problem size $PROBLEM_SIZE"
        run_experiment "mpi_weak_scale_$num" "mpi" "$JAC2D" "$PROBLEM_SIZE 50 -1 1 1" "$num"
done

