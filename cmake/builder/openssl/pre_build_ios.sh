#!/bin/bash
# OpenSSL pre-build for iOS: only exports a CC override when we need to
# force an arch that the chosen ios*-xcrun target does not bake in.
#
# Usage:
#   source pre_build_ios.sh -c "<cc command>"
#
# Typical use — x86_64 simulator on Apple Silicon host:
#   -c "xcrun -sdk iphonesimulator cc -arch x86_64"
#
# When no override is needed (arm64 device, arm64-sim), do NOT source this
# script; OpenSSL's ios*-xcrun target already picks the right CC via xcrun.

set -e

while getopts c: option
do
  case "${option}" in
    c) cc_override=${OPTARG};;
  esac
done

if [ -n "${cc_override}" ]; then
    export CC="${cc_override}"
fi
