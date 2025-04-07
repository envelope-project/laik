#!/bin/bash

trap 'jobs -p | xargs -r kill' SIGINT SIGTERM

procs=1
spares=0
strict_node=-1
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -n) procs="$2"; shift ;;
        -s) spares="$2"; shift ;;
        --strict-node) strict_node="$2"; shift ;;
        -h) echo "Usage: $0 [-n <procs>] [-s <spares>] [--strict-node <0|1>]"; exit 1 ;;
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
# Per node half of the ranks
export LAIK_SIZE=$((procs * 2))
# Ip of gqpu01
export LAIK_UCP_HOST=10.12.100.10

# define NUMA node CPU lists (excluding hyperthreads)
node0_cpus=(0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35)
node1_cpus=(36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71)

num_cpus_node0=${#node0_cpus[@]}
num_cpus_node1=${#node1_cpus[@]}

if [[ "$strict_node" -eq 0 ]]; then
    echo "Strictly allocating on Node 0"
elif [[ "$strict_node" -eq 1 ]]; then
    echo "Strictly allocating on Node 1"
else
    echo "Interleaved allocation"
fi


total=$((procs + spares))
for (( i=0; i<$total; i++ )); do
    sleep 0.15
    if [[ "$strict_node" -eq 0 ]]; then
        #allocation on node 0
        core=${node0_cpus[$((i % num_cpus_node0))]}
    elif [[ "$strict_node" -eq 1 ]]; then
        #allocation on node 1
        core=${node1_cpus[$((i % num_cpus_node1))]}
    else
        #interleaved allocation between node 0 and 1
        if (( i % 2 == 0 )); then
            core=${node0_cpus[$((i / 2 % num_cpus_node0))]}
        else
            core=${node1_cpus[$((i / 2 % num_cpus_node1))]}
        fi
    fi
    numactl --physcpubind=$core -- "$@" &
done
wait
