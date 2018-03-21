#!/bin/sh -eu

run()(
    IFS='_'
    idx="0"

    while [ "${idx}" -lt "${OMPI_COMM_WORLD_SIZE}" ]; do
        ../../examples/${1} &
        idx="$((idx + 1))"
    done

    wait
)

dir="`dirname -- "${0}"`"
name="`basename -- "${0%.sh}"`"

export LAIK_BACKEND='tcp'
export OMPI_COMM_WORLD_SIZE='4'

if [ -f "${dir}/${name}.unsorted" ]; then
    run "${name}" | diff --unified -- "${dir}/${name}.unsorted" -
elif [ -f "${dir}/${name}.sorted" ]; then
    run "${name}" | LC_ALL='C' sort | diff --unified -- "${dir}/${name}.sorted" -
else
    exit 1
fi
