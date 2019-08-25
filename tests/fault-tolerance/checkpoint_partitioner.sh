#!/bin/bash

IFS='_'

export LAIK_BACKEND='tcp'
export LAIK_TCP_CONFIG="`mktemp`"

    printf '%s\n' \
        '[addresses]' \
        "rank0 = localhost $((10000 + ($$ % 5000) * 4 + 0))" \
        "rank1 = localhost $((10000 + ($$ % 5000) * 4 + 1))" \
        "rank2 = localhost $((10000 + ($$ % 5000) * 4 + 2))" \
        "rank3 = localhost $((10000 + ($$ % 5000) * 4 + 3))" \
        > "${LAIK_TCP_CONFIG}"

pwd

for i in 1 2 3 4; do
    ./cmake-build-debug_wsl/tests/fault-tolerance/checkpoint_partitioner &
done

wait

rm -- "${LAIK_TCP_CONFIG}"
