#!/bin/bash

# Set environment variables for UCX installation
export LD_LIBRARY_PATH=/home/ge96hoy2/ucx_install/lib64:$LD_LIBRARY_PATH \
export PKG_CONFIG_PATH=/home/ge96hoy2/ucx_install/lib64/pkgconfig:$PKG_CONFIG_PATH

export LAIK_BACKEND=ucp \
export LAIK_SIZE=2 \
export LAIK_LOG=4 \
export LAIK_UCP_HOST=10.12.100.10 \ 
#export UCX_TLS=rc,ud,rdmacm \
export UCX_TLS=rc,ud,dc,tcp \
export UCX_NET_DEVICES=all

echo "Updated pkg config for UCX"