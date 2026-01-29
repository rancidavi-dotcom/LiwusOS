#!/bin/bash
cd /mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/third_party/tcc-liwusos-build
TCC_SRC=/mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/third_party/tcc

# Compile tcc.c
HOST_CFLAGS="-std=gnu99 -O2 -Wall -Wextra -I$TCC_SRC -DONE_SOURCE=0"
gcc $HOST_CFLAGS -c $TCC_SRC/tcc.c -o tcc-target.o
ls -la tcc-target.o