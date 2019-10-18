#!/bin/bash

module load nano
module load lrztools
module unload mpi.intel
module unload intel
module load gcc

module list

cd ~/laik/cmake-build-debug_wsl || exit

rm -r CMakeCache.txt CMakeFiles/ cmake_install.cmake

CC=$(which gcc)
CXX=$(which g++)
export CC
export CXX

cmake -DCMAKE_SYSTEM_PREFIX_PATH=~/lib/ -DMPI_C_COMPILER=~/lib/bin/mpicc -DMPI_CXX_COMPILER=~/lib/bin/mpicxx -Dtcp-backend=off ../
make

cd ..
