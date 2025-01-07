#!/bin/bash

# Commands to execute
commands=(
  "sudo sysctl -w net.ipv4.tcp_max_syn_backlog=4096"
  "sudo sysctl -w net.core.somaxconn=15000"
  "sudo sysctl -w net.core.netdev_max_backlog=1000"
)

for cmd in "${commands[@]}"; do
ssh "$node" "$cmd"
done
s