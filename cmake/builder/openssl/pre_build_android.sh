#!/bin/bash
# OpenSSL pre-build environment for modern Android NDK (r21+, unified LLVM toolchain).
#
# Usage:
#   source pre_build_android.sh -t <toolchain_root> -h <llvm_triple> -a <triple_with_api>
#
#   -t  NDK LLVM toolchain root, e.g. $NDK/toolchains/llvm/prebuilt/darwin-x86_64
#   -h  LLVM triple, e.g. aarch64-linux-android
#   -a  LLVM triple + API level, e.g. aarch64-linux-android24

set -e

while getopts t:h:a: option
do
  case "${option}" in
    t) toolchain_dir=${OPTARG};;
    h) target_host=${OPTARG};;
    a) target_host_api=${OPTARG};;
  esac
done

if [ -z "${toolchain_dir}" ] || [ -z "${target_host}" ] || [ -z "${target_host_api}" ]; then
  echo "pre_build_android.sh: missing arguments (-t/-h/-a)" >&2
  return 1 2>/dev/null || exit 1
fi

export TOOLCHAIN_PATH="${toolchain_dir}/bin"
export PATH="${TOOLCHAIN_PATH}:${PATH}"

# Derive NDK root from toolchain dir: <NDK>/toolchains/llvm/prebuilt/<host>.
# OpenSSL 3.x's android-* targets read ANDROID_NDK_ROOT to locate sysroot.
export ANDROID_NDK_ROOT="$(cd "${toolchain_dir}/../../../.." && pwd)"
export ANDROID_NDK_HOME="${ANDROID_NDK_ROOT}"

# Modern NDK ships llvm-* wrappers under toolchains/llvm/prebuilt/<host>/bin.
export AR="${TOOLCHAIN_PATH}/llvm-ar"
export AS="${TOOLCHAIN_PATH}/llvm-as"
export LD="${TOOLCHAIN_PATH}/ld"
export STRIP="${TOOLCHAIN_PATH}/llvm-strip"
export RANLIB="${TOOLCHAIN_PATH}/llvm-ranlib"
export NM="${TOOLCHAIN_PATH}/llvm-nm"

# API-level-specific clang wrappers (e.g. aarch64-linux-android24-clang).
export CC="${TOOLCHAIN_PATH}/${target_host_api}-clang"
export CXX="${TOOLCHAIN_PATH}/${target_host_api}-clang++"

# OpenSSL's Configure script for android-* targets looks for ANDROID_NDK_ROOT
# and uses CC/CXX directly. Keep PIC on; OpenSSL 3.x handles PIE itself.
export CFLAGS="-fPIC"
export CPPFLAGS="-fPIC"
export LDFLAGS=""
