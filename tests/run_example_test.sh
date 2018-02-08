#!/bin/sh -eu

group(){
    LC_ALL='C' sort --field-separator=':' --key='1,1' --stable
}

expected="`dirname -- "${0}"`/examples/${LAIK_BACKEND}/${*}.txt"

case "${LAIK_BACKEND}" in
    "mpi")
        mpirun --np '4' --tag-output -- "../examples/${@}" | group
        ;;
    "single")
        "../examples/${@}"
        ;;
esac | cmp - "${expected}"
