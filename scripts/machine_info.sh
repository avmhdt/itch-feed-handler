#!/usr/bin/env bash
# P-05: print the hardware and kernel details that every benchmark row refers to.
set -euo pipefail
lscpu
free -g
lsblk -o NAME,SIZE,ROTA,TYPE,MOUNTPOINT
uname -a
