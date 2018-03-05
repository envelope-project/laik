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
    -I "${SRC}/include" \
    -I "${SRC}/src" \
    "${@}"
