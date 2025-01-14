#!/bin/bash

trap 'jobs -p | xargs -r kill' SIGINT SIGTERM

export LAUNCHER=/home/ubuntu/BachelorThesis/laik/tests/ucp/launcher.sh

DIR=${1:-.}  # default to the current directory
for file in "$DIR"/*; do
    if [ -x "$file" ] && [ -f "$file" ] && [ "$(basename -- "$file")" != "test-kvstest-single.sh" ]; then #kv not supported
        echo "Executing: $file"
        "$file"
    fi
done