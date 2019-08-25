#!/bin/bash

# $1: Name, $2 Type, $3 Executable, $4 Executable_args, $5 run_number
run_experiment () {
	echo "Running experiment $1"
	export MPI_OPTIONS="--output-filename out --merge-stderr-to-stdout --stdin none --oversubscribe"
	export TEST_NAME="$1"
	rm -r out/
	tests/fault-tolerance/launcher.sh "$2" "$5" "$5" release "$3" "$4"
	grep -h -e "===" out/1/rank.*/stdout > "laik_experiments/data/experiment_$1_trace.csv"
	grep -h -e '!!!' out/1/rank.*/stdout > "laik_experiments/data/experiment_$1_header.csv"
}

JAC2D=cmake-build-debug_wsl/tests/fault-tolerance/checkpoint-jac2d-recovery

for num in 01 02 04 08 16 32
do
	run_experiment "mpi_strong_scale_$num" "mpi" "$JAC2D" "1024 50 -1 1 1" "$num"
done


for num in 01 02 04 08 16 32
do
        run_experiment "mpi_weak_scale_$num" "mpi" "$JAC2D" "$(expr 256 \* $num) 50 -1 1 1" "$num"
done

