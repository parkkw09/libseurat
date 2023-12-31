#!/bin/bash

while getopts t:h:a: option
do
 case "${option}"
 in
 t) toolchain_dir=${OPTARG};;
 h) target_host=${OPTARG};;
 a) target_host_api=${OPTARG};;
 esac
done

# Add toolchain to the search path.
export TOOLCHAIN_PATH=$toolchain_dir/bin
PATH=$PATH:$TOOLCHAIN_PATH

# Tell configure what tools to use.
export AR=$target_host-ar
export AS=$target_host-as
export LD=$target_host-ld
export STRIP=$target_host-strip
export RANLIB=$target_host-ranlib

# Tell configure which android api to use.
export CC=$target_host_api-clang
export CXX=$target_host_api-clang++

# Tell configure what flags Android requires.
export CFLAGS="-fPIE -fPIC"
export CPPFLAGS="-fPIE -fPIC"
export LDFLAGS="-pie"