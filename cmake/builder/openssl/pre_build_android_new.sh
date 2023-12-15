#!/bin/bash

while getopts t: option
do
 case "${option}"
 in
 t) toolchain_prefix=${OPTARG};;
 esac
done

# Tell configure what tools to use.
#export AR=${toolchain_prefix}ar
#export AS=${toolchain_prefix}as
#export LD=${toolchain_prefix}ld
#export STRIP=${toolchain_prefix}strip
#export RANLIB=${toolchain_prefix}ranlib

# Tell configure which android api to use.
#export CC=${toolchain_prefix}gcc
#export CXX=${toolchain_prefix}g++

export CROSS_COMPILE=$toolchain_prefix

# Tell configure what flags Android requires.
export CFLAGS="-fPIE -fPIC"
export LDFLAGS="-pie"