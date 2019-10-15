#!/bin/bash

set -x

if [ $# -lt 5 ]
then
	echo "Usage: launcher.sh [tcp|mpi] [numLaunch] [numProcs] [debug|release] [executable] [args]"
	exit
fi

EXECUTABLE="$5"
BACKEND_TYPE="$1"
NUM_LAUNCH="$2"
NUM_PROCS="$3"
RUN_TYPE="$4"
shift 5

TCP_CONFIG=tcp_launcher_config.txt

if [ $RUN_TYPE == "debug" ]
then
	EXECUTABLE="gdb -ex=r -ex=bt -ex=q --args $EXECUTABLE"
fi


if [ $BACKEND_TYPE == "tcp" ]
then

	export LAIK_BACKEND='tcp'
	export LAIK_TCP_CONFIG="$TCP_CONFIG"

	echo -n "
		[general]
		receive_timeout = 0.1
		receive_attempts = 3

		[addresses]
	" > $TCP_CONFIG

	for i in `seq 1 $NUM_PROCS`
	do
	    echo "rank$i = localhost $(expr 10000 + $i)" >> $TCP_CONFIG
	done
fi

echo "Launching $NUM_LAUNCH/$NUM_PROCS processes"
if [ $BACKEND_TYPE == "tcp" ]
then
	for i in `seq 1 $NUM_LAUNCH`
	do
	        $EXECUTABLE $@ &
	        EXIT_CODE=$?
	done
	wait
fi
if [ $BACKEND_TYPE == "mpi" ]
then
	$MPI_RUN $MPI_OPTIONS -n $NUM_LAUNCH $EXECUTABLE $@
  EXIT_CODE=$?
fi

if [ $BACKEND_TYPE == "tcp" ]
then
	rm "$TCP_CONFIG"
fi

#rm -v ./output/data_live_0_0.ppm
echo "Exit code: $EXIT_CODE"
exit $EXIT_CODE
