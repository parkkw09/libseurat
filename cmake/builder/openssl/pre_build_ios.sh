#!/bin/bash

while getopts t:p:s: option
do
 case "${option}"
 in
 t) toolchain_dir=${OPTARG};;
 p) platform_dir=${OPTARG};;
 s) sdk_version=${OPTARG};;
 esac
done

# Add toolchain to the search path.
export TOOLCHAIN_PATH=$toolchain_dir
# PATH=$PATH:$TOOLCHAIN_PATH

export CROSS_TOP=$platform_dir
export CROSS_SDK=$sdk_version

# Tell configure what tools to use.
export AR=$TOOLCHAIN_PATH/ar
export AS=$TOOLCHAIN_PATH/as
export LD=$TOOLCHAIN_PATH/ld
export STRIP=$TOOLCHAIN_PATH/strip
export RANLIB=$TOOLCHAIN_PATH/ranlib

# Tell configure which android api to use.
export CC=$TOOLCHAIN_PATH/clang
export CXX=$TOOLCHAIN_PATH/clang++