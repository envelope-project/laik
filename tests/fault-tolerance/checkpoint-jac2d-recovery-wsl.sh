#!/bin/bash

wsl-start -h always /bin/bash -c "tests/fault-tolerance/checkpoint-jac2d-recovery.sh" &
wsl-start -p left -h always /bin/bash -c "tests/fault-tolerance/checkpoint-jac2d-recovery.sh" &
wsl-start -p right -h always /bin/bash -c "tests/fault-tolerance/checkpoint-jac2d-recovery.sh"

wait

