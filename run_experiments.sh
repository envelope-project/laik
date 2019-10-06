#!/bin/bash
#!/bin/bash
# Job Name and Files (also --job-name)
#SBATCH -J LAIK_FAULT_TOLERANCE
#Output and error (also --output, --error):
#SBATCH -o ./%x.%j.out
#SBATCH -e ./%x.%j.err
#Initial working directory (also --chdir):
#SBATCH -D ./
#Notification and type
#SBATCH --mail-type=END
#SBATCH --mail-user=vincent.bode@tum.de
# Wall clock limit:
#SBATCH --time=00:30:00
#SBATCH --no-requeue
#Setup of execution environment
#SBATCH --export=NONE
#SBATCH --get-user-env
#SBATCH --account=pn72xo
#constraints are optional
##--constraint="scratch&work"

#SBATCH --partition=test
#Number of nodes and MPI tasks per node:
###SBATCH --nodes=1
#SBATCH --ntasks=48
###SBATCH --ntasks-per-node=48

#SBATCH --ear=off

#Important
module load slurm_setup


#set -x

export PATH="/u/home/bodev/lib/bin:$PATH"
export MPI_RUN="/dss/dsshome1/08/ga26poh3/lib/bin/mpirun"

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
OSU=$PREFIX/tests/fault-tolerance/osu/checkpoint-osu-latency-ring
JAC2D=$PREFIX/tests/fault-tolerance/checkpoint-jac2d-recovery
LULESH=$PREFIX/tests/fault-tolerance/lulesh/lulesh

if [ ! -f "$OSU" ] || [ ! -f "$JAC2D" ] || [ ! -f "$LULESH" ]
then
  echo "MISSING Executable"
  exit 255
fi



if [ $# -ne 1 ]
then
  echo "Need to pass which experiment to run"
  exit 255
fi

OSU_CONF_A="-m 4096:4096 -i 10000"
JAC2D_CONF_A="20000 100 -1"
LULESH_CONF_A="-i 50 -s 8"

case "$1" in
  "runtime_time")
    for i in {1..20} ; do
      run_experiment "runtime_time_mpi_osu_$i" "mpi" "$OSU" "$OSU_CONF_A" "48"
      run_experiment "runtime_time_mpi_jac2d_$i" "mpi" "$JAC2D" "$JAC2D_CONF_A" "48"
      run_experiment "runtime_time_mpi_lulesh_$i" "mpi" "$LULESH" "$LULESH_CONF_A" "27"
    done
  ;;
  "restart_time")
    for i in {1..5} ; do
      run_experiment "runtime_time_mpi_jac2d_$i" "mpi" "$JAC2D" "$JAC2D_CONF_A" "48"
    done
  ;;

  "old")
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
    run_experiment "mpi_lulesh_single_failure" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 -repart 1 $LULESH_OPTIONS" "$num"
    run_experiment "mpi_lulesh_no_checkpoint_single_failure" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --checkpointFrequency 0 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 --skipCheckpointRecovery -repart 1 $LULESH_OPTIONS" "$num"

    num=27
    #run_experiment "mpi_lulesh_double_failure_sequential" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --plannedFailure 3 39 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $LULESH_OPTIONS" "$num"
    #run_experiment "mpi_lulesh_double_failure_parallel" "mpi" "$EXECUTABLE" "--plannedFailure 1 29 --plannedFailure 3 29 --checkpointFrequency 10 --redundancyCount 1 --rotationDistance 1 --failureCheckFrequency 10 $LULESH_OPTIONS" "$num"

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
    ;;
esac