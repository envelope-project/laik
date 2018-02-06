#!/bin/sh -eu

order(){
    LC_ALL='C' sort --field-separator=':' --key='1,1' --output="${1}" --stable -- "${1}"
}

backend="${1}"
stdout="${2}"
stderr="${3}"
expected="${4}"

shift 4

export LAIK_LOG='1'

case "${backend}" in
    "mpi")
        mpirun --np '4' --tag-output -- "${@}" 0<'/dev/null' 1>"${stdout}" 2>"${stderr}"
        order "${stdout}"
        order "${stderr}"
        ;;
    "single")
        "${@}" 0<'/dev/null' 1>"${stdout}" 2>"${stderr}"
        ;;
    *)
        exit 1
        ;;
esac

cmp -- "${stdout}" "${expected}"
