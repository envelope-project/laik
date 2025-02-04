#!/bin/bash

# Set environment variables for UCX installation
export LD_LIBRARY_PATH=/home/ge96hoy2/ucx_install/lib64:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=/home/ge96hoy2/ucx_install/lib64/pkgconfig:$PKG_CONFIG_PATH

echo "Updated pkg config for UCX"