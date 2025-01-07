#!/bin/bash

trap 'jobs -p | xargs -r kill' SIGINT SIGTERM

procs=1
spares=0
remove=""
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -n) procs="$2"; shift ;;
        -s) spares="$2"; shift ;;
        -r) remove="$2"; shift ;;
        -h) echo "Usage: $0 [-n <procs>] [-s <spares>] [-r <removes>]"; exit 1 ;;
        -*) echo "Unknown parameter passed: $1"; exit 1 ;;
        *) break;;
    esac
    shift
done

if [ -z "$1" ]; then
    echo "Error: no command given"
    exit 1
fi

export LAIK_BACKEND=ucp
export LAIK_SIZE=$procs

total=$((procs + spares))
for (( i=1; i<=$total; i++ )); do
    log_file="output_rank_${i}.log"
    echo "Launching process $i, output redirected to $log_file"
    $@ > "$log_file" 2>&1 &  # Redirect stdout and stderr to the log file
done

[ ! -z "$remove" ] && sleep .1 && echo "c $remove" | nc localhost 7777 &> /dev/null
wait
