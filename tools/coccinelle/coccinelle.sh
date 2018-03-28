#!/bin/sh -eu

DIR="`dirname -- "${0}"`"
SRC="${DIR}/../.."

# Without any arguments, apply all patches
if [ "${#}" -eq 0 ]; then
    set -- "${DIR}"/*.patch
fi

# Apply the selected set of patches
for patch in "${@}"; do
    echo "Applying ${patch}"

    for dir in 'examples' 'external' 'src'; do
        spatch \
            --dir "${SRC}/${dir}" \
            --in-place \
            --sp-file "${patch}" \
            --very-quiet
    done
done
