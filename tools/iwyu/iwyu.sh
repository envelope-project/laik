#!/bin/sh -eu

DIR="`dirname -- "${0}"`"
SRC="${DIR}/../.."

for package in \
    'glib-2.0' \
    'libprotobuf-c' \
    'mpi' \
    'papi'
do
    if pkg-config --exists "${package}"; then
        set -- `pkg-config --cflags-only-I "${package}"` "${@}"
    fi
done

include-what-you-use \
    -Xiwyu --mapping_file="${DIR}/map.yaml" \
    -D LAIK_TCP_DEBUG \
    -D LAIK_TCP_STATS \
    -D USE_MPI \
    -D USE_SHMEM \
    -D USE_TCP \
    -I "${SRC}/include" \
    -I "${SRC}/src" \
    "${@}"
