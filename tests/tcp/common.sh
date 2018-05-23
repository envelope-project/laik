#!/bin/bash -eu

run()(
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

    for i in 1 2 3 4; do
        ../../examples/${1} &
    done

    wait

    rm -- "${LAIK_TCP_CONFIG}"
)

dir="`dirname -- "${0}"`"
name="`basename -- "${0%.sh}"`"

if [ -f "${dir}/${name}.unsorted" ]; then
    run "${name}" | diff --unified -- "${dir}/${name}.unsorted" -
elif [ -f "${dir}/${name}.sorted" ]; then
    run "${name}" | LC_ALL='C' sort | diff --unified -- "${dir}/${name}.sorted" -
else
    exit 1
fi
