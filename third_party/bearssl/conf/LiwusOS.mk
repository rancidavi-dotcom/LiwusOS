# Configuration for LiwusOS (bare-metal, no stack protection)

# Build directory.
BUILD = build

# Extension for executable files.
E =

# Extension for object files.
O = .o

# Prefix for library file name.
LP = lib

# Extension for library file name.
L = .a

# Prefix for DLL file name.
DP = lib

# Extension for DLL file name.
D = .so

# File deletion tool.
RM = rm -f

# Directory creation tool.
MKDIR = mkdir -p

# C compiler and flags.
CC = gcc
CFLAGS = -W -Wall -Os -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
CCOUT = -c -o 

# Static library building tool.
AR = ar
ARFLAGS = -rcs
AROUT =

# DLL building tool - disabled for bare-metal
DLL = no

# Static linker.
LD = cc
LDFLAGS = 
LDOUT = -o 

# C# compiler; we assume usage of Mono.
MKT0COMP = mk$PmkT0.sh
RUNT0COMP = mono T0Comp.exe

# Disable DLL and tests by default
DLL = no
TESTS = no