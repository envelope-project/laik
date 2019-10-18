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


set -x

export PATH="/u/home/bodev/lib/bin:$PATH"
if [ -x "/dss/dsshome1/08/ga26poh3/lib/bin/mpirun" ]
then
  echo "Using SuperMUC Configuration"
  export MPI_RUN="/dss/dsshome1/08/ga26poh3/lib/bin/mpirun"
else
  echo "Using standard MPI_RUN"
  MPI_RUN="$(which mpirun)"
  export MPI_RUN
fi

# $1: Name, $2 Type, $3 Executable, $4 Executable_args, $5 run_number
run_experiment () {
	echo "Running experiment $1"
	export MPI_OPTIONS="$MPI_OPTIONS --output-filename out --merge-stderr-to-stdout --stdin none"
#	export MPI_OPTIONS="--output-filename out --merge-stderr-to-stdout --stdin none --mca mpi_ft_detector true"
#	export MPI_OPTIONS="$MPI_OPTIONS --oversubscribe"
	export TEST_NAME="$1"
	rm -r out/
	tests/fault-tolerance/launcher.sh "$2" "$5" "$5" release "$3" "$4"
	EXIT_CODE=$?
	grep -h -e "===" out/1/rank.*/stdout > "laik_experiments/data/experiment_$1_trace.csv"
#	grep -h -e '!!!' out/1/rank.*/stdout > "laik_experiments/data/experiment_$1_header.csv"

	if [ $EXIT_CODE -ne 0 ] && [ "$EXPERIMENT_IGNORE_EXIT_CODE" -ne 1 ]
	then
	  echo "Error: $EXIT_CODE"
	  exit $EXIT_CODE
  fi
}

benchmark_concat_options () {
  if [ "$#" -ne 3 ]
  then
    echo "Incorrect parameters to concat"
    exit 255
  fi
  case "$1" in
    "osu")
      echo "$2 -- $1"
      ;;
    "jac2d"|"lulesh")
      echo "$1 $2"
      ;;
    *)
      echo "Unknown benchmark"
      exit 255
      ;;
  esac
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



if [ $# -ne 2 ]
then
  echo "Need to pass which experiment to run and the benchmark"
  exit 255
fi

#OSU_CONF_A="-m 32768:32768 -i 250000"
#JAC2D_CONF_A="20000 50 -1"
#LULESH_CONF_A="-i 280 -s 14"
OSU_CONF_A="-m 32768:32768 -i 2150000"
JAC2D_CONF_A="25500 300 -1"
LULESH_CONF_A="-i 3200 -s 16"

#Only includes ranks up to 27 to accomodate for LULESH
FAILURE_RANKS=(22 26 13 18 26 1 4 13 19 16)
case "$2" in
  "osu")
    BENCHMARK="osu"
    BENCHMARK_EXECUTABLE="$OSU"
    BENCHMARK_OPTIONS="$OSU_CONF_A"
    NUM_PROCESSES=48
    FAILURE_ITERATIONS=(142266 128688 121441 118626 44627 86271 25756 155891 123766 90289)
    ;;
  "jac2d")
    BENCHMARK="jac2d"
    BENCHMARK_EXECUTABLE="$JAC2D"
    BENCHMARK_OPTIONS="$JAC2D_CONF_A"
    NUM_PROCESSES=48
    FAILURE_ITERATIONS=(14297 671 21445 21745 2790 11552 14735 14808 15161 19657)
    ;;
  "lulesh")
    BENCHMARK="lulesh"
    BENCHMARK_EXECUTABLE="$LULESH"
    BENCHMARK_OPTIONS="$LULESH_CONF_A"
    NUM_PROCESSES=27
    FAILURE_ITERATIONS=(2985 2485 2507 1226 2460 1096 1917 1391 1914 5)
    ;;
  "all")
    echo "ALL: Skipping variable setting"
    ;;
  *)
    echo "Unknown benchmark"
    exit 255
    ;;
esac

case "$1" in
  "timing_test")
      time run_experiment "timing_test_osu" "mpi" "$OSU" "$OSU_CONF_A" "48"
      time run_experiment "timing_test_jac2d" "mpi" "$JAC2D" "$JAC2D_CONF_A" "48"
      time run_experiment "timing_test_lulesh_$i" "mpi" "$LULESH" "$LULESH_CONF_A" "27"
  ;;
  "runtime_time")
    for i in {0..9} ; do
      run_experiment "runtime_time_mpi_${BENCHMARK}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$BENCHMARK_OPTIONS" "$NUM_PROCESSES"
    done
  ;;
  "restart_time")
    MPI_OPTIONS=""
    for i in {0..9} ; do
      OPTIONS=$(benchmark_concat_options "--plannedFailure ${FAILURE_RANKS[i]} ${FAILURE_ITERATIONS[i]}" "$BENCHMARK_OPTIONS")
      run_experiment "restart_time_mpi_${BENCHMARK}_$i" "mpi" "$BENCHMARK" "$OPTIONS" "$NUM_PROCESSES"
    done
  ;;
  "restart_time_mca")
    EXPERIMENT_IGNORE_EXIT_CODE=1
    MPI_OPTIONS="--mca orte_enable_recovery false"
    for i in {0..9} ; do
      OPTIONS=$(benchmark_concat_options "--plannedFailure ${FAILURE_RANKS[i]} ${FAILURE_ITERATIONS[i]}" "$BENCHMARK_OPTIONS")
      run_experiment "restart_time_mpi_${BENCHMARK}_$i" "mpi" "$BENCHMARK" "$OPTIONS" "$NUM_PROCESSES"
    done
  ;;

  "recovery_time")

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
  *)
    echo "Unknown experiment"
    exit 255
esac