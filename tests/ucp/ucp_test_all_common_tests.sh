#!/bin/bash

trap 'jobs -p | xargs -r kill' SIGINT SIGTERM

export LAUNCHER="/home/ge96hoy2/laik/tests/ucp/launcher.sh"

DIR="/home/ge96hoy2/laik/tests/common"

for file in "$DIR"/*; do
    if [ -x "$file" ] && [ -f "$file" ]; then 
        #echo "Executing: $file"
        "$file"
    fi
done