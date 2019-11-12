#!/bin/bash
#!/bin/bash
# Job Name and Files (also --job-name)
#SBATCH -J LAIK_FAULT_TOLERANCE
#Output and error (also --output, --error):
#SBATCH -o ./%x.out
#SBATCH -e ./%x.err
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
module load python/3.6_intel

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

# $1: Name, $2 Type, $3 Executable, $4 Executable_args, $5 run_number, $6 append index
run_experiment () {
	echo "Running experiment $1"
	export MPI_OPTIONS="$MPI_OPTIONS --output-filename out --merge-stderr-to-stdout --stdin none --mca orte_tmpdir_base /tmp/ga26poh3/"
#	export MPI_OPTIONS="--output-filename out --merge-stderr-to-stdout --stdin none --mca mpi_ft_detector true"
#	export MPI_OPTIONS="$MPI_OPTIONS --oversubscribe"
	export TEST_NAME="$1"
	export LAIK_APPLICATION_TRACE_ENABLED=1
	export LAIK_LOG=3
	rm -r out/
	tests/fault-tolerance/launcher.sh "$2" "$5" "$5" release "$3" "$4"
	EXIT_CODE=$?

	if [ "$EXPERIMENT_TRACE_APPEND" -ne 1 ]
	then
	  echo "Creating a new trace"
	  grep -h -e "===" out/1/rank.*/stdout > "laik_experiments/data/experiment_$1_trace.csv"
  else
    echo "Appending to previous trace"
    if [ "$6" -eq 0 ]
    then
      echo "[DEACTIVATED] Clearing earlier trace due to test 0"
#      rm -v "laik_experiments/data/experiment_$1_trace.csv"
    fi
	  grep -h -e "===" out/1/rank.*/stdout >> "laik_experiments/data/experiment_$1_trace.csv"
	  sed -i -e 's/===,EVENT_SEQ,EVENT_TYPE,RANK,TIME,DURATION,WALLTIME,ITER,MEM,NET,EXTRA//g' -e '1s/^/===,EVENT_SEQ,EVENT_TYPE,RANK,TIME,DURATION,WALLTIME,ITER,MEM,NET,EXTRA/' "laik_experiments/data/experiment_$1_trace.csv"
  fi
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
      echo "$3 -- $2"
      ;;
    "jac2d"|"lulesh")
      echo "$2 $3"
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



if [ $# -lt 2 ]
then
  echo "Need to pass which experiment to run and the benchmark"
  exit 255
fi

#OSU_CONF_A="-m 32768:32768 -i 250000"
#JAC2D_CONF_A="20000 50 -1"
#LULESH_CONF_A="-i 280 -s 14"

OSU_CONF_B="-m 32768:32768 -i 100"
JAC2D_CONF_B="4096 30 -1"
LULESH_CONF_B="-s 17 -i 20"

EXPERIMENT_TRACE_APPEND=0
TEST_MIN=0
TEST_MAX=1

TEST_MIN_2=1
TEST_MAX_2=3

#Only includes ranks up to 27 to accomodate for LULESH
FAILURE_RANKS=(22 26 13 18 26 1 4 13 19 16)
RESTART_FAILURE_RANKS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26)
case "$2" in
  "osu")
    BENCHMARK="osu"
    BENCHMARK_EXECUTABLE="$OSU"
    BENCHMARK_ITER=2150000
    OSU_CONF_A="-m 32768:32768 -i $BENCHMARK_ITER"
    BENCHMARK_OPTIONS="$OSU_CONF_A"
    BENCHMARK_OPTIONS_B="$OSU_CONF_B"
    BENCHMARK_CHECKPOINT_SETTINGS="--checkpointFrequency 200000 --failureCheckFrequency 200000 --redundancyCount 1 --rotationDistance 1"
    NUM_PROCESSES=48
    FAILURE_ITERATIONS=(142266 128688 121441 118626 44627 86271 25756 155891 123766 90289)
    RESTART_FAILURE_ITERATIONS=(182737 -1 119668 360211 1192434 -1 997321 1356872 -1 1226131 1560360 -1 -1 927938 -1 258380 1196613 1413304 -1 458820 -1 -1 623058 844874 -1)
    ;;
  "jac2d")
    BENCHMARK="jac2d"
    BENCHMARK_EXECUTABLE="$JAC2D"
    BENCHMARK_ITER=10600
    JAC2D_CONF_A="--progressReportInterval 100 4096 $BENCHMARK_ITER -1"
    BENCHMARK_OPTIONS="$JAC2D_CONF_A"
    BENCHMARK_OPTIONS_B="$JAC2D_CONF_B"
    BENCHMARK_CHECKPOINT_SETTINGS="--checkpointFrequency 2000 --failureCheckFrequency 500 --redundancyCount 1 --rotationDistance 1"
    NUM_PROCESSES=48
    FAILURE_ITERATIONS=(2214 6897 3384 4608 9296 7218 2518 2008 6843 1558)
    RESTART_FAILURE_ITERATIONS=(190 4723 -1 2665 3374 6259 -1 -1 952 1783 3355 4413 -1 -1 959 -1 9266 -1 8035 8160 8255 -1 7542 -1 4182 4693 4841 6616 8471 9428 -1)
    ;;
  "lulesh")
    BENCHMARK="lulesh"
    BENCHMARK_EXECUTABLE="$LULESH"
    BENCHMARK_ITER=1340
    LULESH_CONF_A="-s 17 -i $BENCHMARK_ITER"
    BENCHMARK_OPTIONS="$LULESH_CONF_A"
    BENCHMARK_OPTIONS_B="$LULESH_CONF_B"
    BENCHMARK_CHECKPOINT_SETTINGS="--checkpointFrequency 200 --failureCheckFrequency 50 --redundancyCount 1 --rotationDistance 1 -repart 1"
    NUM_PROCESSES=27
    FAILURE_ITERATIONS=(880 238 415 1179 656 718 480 42 225 475)
    RESTART_FAILURE_ITERATIONS=(316 495 -1 640 -1 179 1083 -1 753 -1 242 1059 -1 74 115 -1 215 527 1190 -1 316 -1 177 865 -1 271 401 915 -1)
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
#      time run_experiment "timing_test_osu" "mpi" "$OSU" "$OSU_CONF_A" "48" "0"
#      time run_experiment "timing_test_jac2d" "mpi" "$JAC2D" "$JAC2D_CONF_A" "48" "0"
#      time run_experiment "timing_test_lulesh_$i" "mpi" "$LULESH" "$LULESH_CONF_A" "27" "0"
  ;;
  "runtime_time")
    for ((i = TEST_MIN; i < TEST_MAX; i++)); do
      MPI_OPTIONS=""
      run_experiment "runtime_time_mpi_${BENCHMARK}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$BENCHMARK_OPTIONS" "$NUM_PROCESSES" "$i"
    done
  ;;
  "restart_time")
    for ((i = TEST_MIN; i < TEST_MAX; i++)); do
      MPI_OPTIONS=""
      OPTIONS=$(benchmark_concat_options "$BENCHMARK" "--plannedFailure ${FAILURE_RANKS[i]} ${FAILURE_ITERATIONS[i]}" "$BENCHMARK_OPTIONS")
      run_experiment "restart_time_mpi_${BENCHMARK}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$OPTIONS" "$NUM_PROCESSES" "$i"
    done
  ;;
  "restart_time_mca")
    EXPERIMENT_IGNORE_EXIT_CODE=1
    for ((i = TEST_MIN; i < TEST_MAX; i++)); do
      MPI_OPTIONS="--mca orte_enable_recovery false"
      OPTIONS=$(benchmark_concat_options "$BENCHMARK" "--plannedFailure ${FAILURE_RANKS[i]} ${FAILURE_ITERATIONS[i]}" "$BENCHMARK_OPTIONS")
      run_experiment "restart_time_mca_mpi_${BENCHMARK}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$OPTIONS" "$NUM_PROCESSES" "$i"
    done
  ;;
  "recovery_time")
    EXPERIMENT_IGNORE_EXIT_CODE=1
    for ((i = TEST_MIN; i < TEST_MAX; i++)); do
      MPI_OPTIONS=""
      OPTIONS=$(benchmark_concat_options "$BENCHMARK" "$BENCHMARK_CHECKPOINT_SETTINGS --plannedFailure ${FAILURE_RANKS[i]} ${FAILURE_ITERATIONS[i]}" "$BENCHMARK_OPTIONS")
      run_experiment "recovery_time_mpi_${BENCHMARK}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$OPTIONS" "$NUM_PROCESSES" "$i"
    done
  ;;
  "restart_time_to_solution")
    EXPERIMENT_IGNORE_EXIT_CODE=1
    EXPERIMENT_TRACE_APPEND=1
    FAILURE_INDEX=0
    rm -v "laik_experiments/data/experiment_restart_time_to_solution_mpi_${BENCHMARK}_*.csv"
    for ((i = TEST_MIN; i < TEST_MAX; i++)); do
      while true
      do
        MPI_OPTIONS="--mca orte_enable_recovery false"
        CURRENT_ITER="${RESTART_FAILURE_ITERATIONS[$FAILURE_INDEX]}"
        OPTIONS=$(benchmark_concat_options "$BENCHMARK" "--plannedFailure ${RESTART_FAILURE_RANKS[$FAILURE_INDEX]} ${CURRENT_ITER}" "$BENCHMARK_OPTIONS")
        ## Attention! Not $i but $FAILURE_INDEX for append detection
        run_experiment "restart_time_to_solution_mpi_${BENCHMARK}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$OPTIONS" "$NUM_PROCESSES" "$FAILURE_INDEX"
        if  [ "${RESTART_FAILURE_ITERATIONS[$FAILURE_INDEX]}" -eq -1 ]
        then
          ((FAILURE_INDEX++))
          break
        fi
        ((FAILURE_INDEX++))
      done
    done
  ;;
  "checkpoint_time_to_solution")
    EXPERIMENT_IGNORE_EXIT_CODE=1
    EXPERIMENT_TRACE_APPEND=0
    FAILURE_INDEX=0
    for ((i = TEST_MIN; i < TEST_MAX; i++)); do
      OPTION_STRING="$BENCHMARK_CHECKPOINT_SETTINGS"
      while [ "${RESTART_FAILURE_ITERATIONS[$FAILURE_INDEX]}" -ne -1 ]
      do
        CURRENT_ITER="${RESTART_FAILURE_ITERATIONS[$FAILURE_INDEX]}"
        OPTION_STRING="$OPTION_STRING --plannedFailure ${RESTART_FAILURE_RANKS[$FAILURE_INDEX]} ${CURRENT_ITER}"
        ((FAILURE_INDEX++))
      done
      ((FAILURE_INDEX++))
      MPI_OPTIONS="--mca mpi_ft_detector true"
      OPTIONS=$(benchmark_concat_options "$BENCHMARK" "$OPTION_STRING" "$BENCHMARK_OPTIONS")
      run_experiment "checkpoint_time_to_solution_mpi_${BENCHMARK}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$OPTIONS" "$NUM_PROCESSES" "$i"
    done
  ;;

  "demo")
    EXPERIMENT_IGNORE_EXIT_CODE=1
    EXPERIMENT_TRACE_APPEND=0


    for ((i = TEST_MIN; i < TEST_MAX; i++)); do
      OPTIONS=$(benchmark_concat_options "$BENCHMARK" "--checkpointFrequency 10 --failureCheckFrequency 10 --redundancyCount 1 --rotationDistance 1 --plannedFailure 1 15 --progressReportInterval 1" "$BENCHMARK_OPTIONS_B" )
      NUM_PROCESSES=4
      BENCHMARK_EXECUTABLE="valgrind --tool=massif --time-unit=ms --detailed-freq=1000000 --depth=1 --max-snapshots=500 $BENCHMARK_EXECUTABLE"
      run_experiment "demo_${BENCHMARK}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$OPTIONS" "$NUM_PROCESSES" "$i"
    done
  ;;

  "demo-late-release")
    EXPERIMENT_IGNORE_EXIT_CODE=1
    EXPERIMENT_TRACE_APPEND=0


    for ((i = TEST_MIN; i < TEST_MAX; i++)); do
      OPTIONS=$(benchmark_concat_options "$BENCHMARK" "--checkpointFrequency 10 --failureCheckFrequency 10 --redundancyCount 1 --rotationDistance 1 --plannedFailure 1 15 --progressReportInterval 1 --delayCheckpointRelease" "$BENCHMARK_OPTIONS_B" )
      NUM_PROCESSES=4
      BENCHMARK_EXECUTABLE="valgrind --tool=massif --time-unit=ms --detailed-freq=1000000 --depth=1 --max-snapshots=500 $BENCHMARK_EXECUTABLE"
      run_experiment "demo_${BENCHMARK}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$OPTIONS" "$NUM_PROCESSES" "$i"
    done
  ;;

  "scaling")
    EXPERIMENT_IGNORE_EXIT_CODE=1
    EXPERIMENT_TRACE_APPEND=0

    if [ $# -ne 3 ]
    then
      echo "Need to pass the number of processes to run"
      exit 255
    fi

    for ((i = TEST_MIN_2; i < TEST_MAX_2; i++)); do
      MPI_OPTIONS="--mca mpi_ft_detector true"
      NUM_PROCESSES=$3
      OPTIONS=$(benchmark_concat_options "$BENCHMARK" "$BENCHMARK_CHECKPOINT_SETTINGS --plannedFailure $(( NUM_PROCESSES - 1 )) $(( BENCHMARK_ITER / 3 )) --plannedFailure $(( NUM_PROCESSES - 2 )) $(( BENCHMARK_ITER * 2 / 3))" "$BENCHMARK_OPTIONS")
      run_experiment "mpi_scale_${BENCHMARK}_${NUM_PROCESSES}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$OPTIONS" "$NUM_PROCESSES" "$i"
    done
  ;;
  "weak-scaling")
    EXPERIMENT_IGNORE_EXIT_CODE=1
    EXPERIMENT_TRACE_APPEND=0

    if [ $# -ne 3 ]
    then
      echo "Need to pass the number of processes to run"
      exit 255
    fi

    for ((i = TEST_MIN_2; i < TEST_MAX_2; i++)); do
      MPI_OPTIONS="--mca mpi_ft_detector true --oversubscribe"
      NUM_PROCESSES=$3
      BENCHMARK_GRID_SIZE=$(python3 -c "import math; print(math.floor(math.sqrt( $NUM_PROCESSES ) * 591))" )
      OPTIONS=$(benchmark_concat_options "$BENCHMARK" "$BENCHMARK_CHECKPOINT_SETTINGS --plannedFailure $(( NUM_PROCESSES - 1 )) $(( BENCHMARK_ITER / 3 )) --plannedFailure $(( NUM_PROCESSES - 2 )) $(( BENCHMARK_ITER * 2 / 3))" "--progressReportInterval 100 $BENCHMARK_GRID_SIZE $BENCHMARK_ITER -1")
      run_experiment "mpi_scale_weak_${BENCHMARK}_${NUM_PROCESSES}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$OPTIONS" "$NUM_PROCESSES" "$i"
    done
  ;;
  "scaling-lulesh")
    EXPERIMENT_IGNORE_EXIT_CODE=1
    EXPERIMENT_TRACE_APPEND=0

    if [ $# -ne 4 ]
    then
      echo "Need to pass the number of processes to run and the problem size"
      exit 255
    fi

    for ((i = TEST_MIN_2; i < TEST_MAX_2; i++)); do
      MPI_OPTIONS="--oversubscribe"
      CUBE_ROOT=$3
      NUM_PROCESSES=$(( CUBE_ROOT * CUBE_ROOT * CUBE_ROOT ))
      SMALLER_CUBE_ROOT=$(( CUBE_ROOT - 1))
      NUM_REPART_PROCESSES=$(( SMALLER_CUBE_ROOT * SMALLER_CUBE_ROOT * SMALLER_CUBE_ROOT ))

      PROBLEM_SIZE=$4
      NUM_ITEMS=$(( NUM_PROCESSES * PROBLEM_SIZE))
      REMAINDER=$(( NUM_ITEMS % NUM_REPART_PROCESSES ))
      if [ $REMAINDER -ne 0 ]
      then
        echo "Repart problem size is imbalanced."
        exit 255
      fi
      OPTIONS=$(benchmark_concat_options "$BENCHMARK" "$BENCHMARK_CHECKPOINT_SETTINGS -repart $NUM_REPART_PROCESSES --plannedFailure $(( NUM_PROCESSES - 1 )) $(( BENCHMARK_ITER / 3 )) --plannedFailure $(( NUM_PROCESSES - 2 )) $(( BENCHMARK_ITER * 2 / 3))" "$BENCHMARK_OPTIONS -s $PROBLEM_SIZE")
      run_experiment "mpi_scale_${BENCHMARK}_${NUM_PROCESSES}_$i" "mpi" "$BENCHMARK_EXECUTABLE" "$OPTIONS" "$NUM_PROCESSES" "$i"
    done
  ;;

  "demo-visualizer")
    python3 ./laik_experiments/visualizer.py "/bin/bash" "-c" "MPI_OPTIONS=\"\" tests/fault-tolerance/launcher.sh \"mpi\" \"$NUM_PROCESSES\" \"$NUM_PROCESSES\" release \"$BENCHMARK_EXECUTABLE\" \"--checkpointFrequency 10 --failureCheckFrequency 10 --redundancyCount 1  --rotationDistance 1 --progressReportInterval 1 $JAC2D_CONF_B\""
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