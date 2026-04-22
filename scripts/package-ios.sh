#!/bin/bash
# =============================================================================
# iOS packaging: build one .xcframework per built library (openssl, openh264,
# libyuv, srt, seurat-*) combining the arm64 device slice and a simulator slice.
#
# Per LEONARDO_ARCH, the build places artifacts at:
#   out/arm64/        (iphoneos device)
#   out/arm64-sim/    (iphonesimulator arm64)
#   out/x86_64/       (iphonesimulator x86_64)
#
# We lipo-merge both simulator slices into one fat archive, then run
# `xcodebuild -create-xcframework` to produce Seurat.xcframework.
# =============================================================================

set -e

OUT_ROOT="$1"      # out/
DIST_DIR="$2"      # dist/ios
ARCHS="$3"         # "arm64 arm64-sim x86_64"

if [ -z "${OUT_ROOT}" ] || [ -z "${DIST_DIR}" ] || [ -z "${ARCHS}" ]; then
    echo "Usage: package-ios.sh <out_root> <dist_dir> <archs>" >&2
    exit 1
fi

mkdir -p "${DIST_DIR}"
STAGE="${DIST_DIR}/_stage"
rm -rf "${STAGE}"
mkdir -p "${STAGE}/sim" "${STAGE}/device"

# Discover libraries that were built (look at first available arch for lib list).
FIRST_ARCH=""
for ARCH in ${ARCHS}; do
    if [ -d "${OUT_ROOT}/${ARCH}/lib" ]; then
        FIRST_ARCH="${ARCH}"
        break
    fi
done

if [ -z "${FIRST_ARCH}" ]; then
    echo "ERROR: no built libraries found under ${OUT_ROOT}/{${ARCHS}}/lib" >&2
    exit 1
fi

LIBS=$(find "${OUT_ROOT}/${FIRST_ARCH}/lib" -maxdepth 1 -type f -name "*.a" -exec basename {} \; | sort -u)

# Collect simulator slices that actually exist.
SIM_SLICES=""
for sim in arm64-sim x86_64; do
    if [ -d "${OUT_ROOT}/${sim}/lib" ]; then
        SIM_SLICES="${SIM_SLICES} ${sim}"
    fi
done

for LIB in ${LIBS}; do
    echo "--- Packaging ${LIB}"

    # ---- Device slice (arm64) ----
    DEV_A="${OUT_ROOT}/arm64/lib/${LIB}"
    if [ ! -f "${DEV_A}" ]; then
        echo "    WARN: missing device slice ${DEV_A}, skipping"
        continue
    fi
    DEV_STAGE="${STAGE}/device/${LIB%.a}"
    mkdir -p "${DEV_STAGE}"
    cp "${DEV_A}" "${DEV_STAGE}/${LIB}"

    # ---- Simulator slice (lipo merge arm64-sim + x86_64 if both exist) ----
    SIM_STAGE="${STAGE}/sim/${LIB%.a}"
    mkdir -p "${SIM_STAGE}"
    SIM_INPUTS=""
    for sim in ${SIM_SLICES}; do
        if [ -f "${OUT_ROOT}/${sim}/lib/${LIB}" ]; then
            SIM_INPUTS="${SIM_INPUTS} ${OUT_ROOT}/${sim}/lib/${LIB}"
        fi
    done

    if [ -z "${SIM_INPUTS}" ]; then
        echo "    WARN: no simulator slices for ${LIB}; device-only xcframework"
        SIM_FRAMEWORK_ARG=""
    else
        lipo -create ${SIM_INPUTS} -output "${SIM_STAGE}/${LIB}"
        SIM_FRAMEWORK_ARG="-library ${SIM_STAGE}/${LIB}"
    fi

    # ---- Headers: reuse from device build if present ----
    DEV_HEADERS="${OUT_ROOT}/arm64/include"
    DEV_HEADER_ARG=""
    SIM_HEADER_ARG=""
    if [ -d "${DEV_HEADERS}" ]; then
        DEV_HEADER_ARG="-headers ${DEV_HEADERS}"
        SIM_HEADER_ARG="-headers ${DEV_HEADERS}"
    fi

    OUT_FRAMEWORK="${DIST_DIR}/${LIB%.a}.xcframework"
    rm -rf "${OUT_FRAMEWORK}"

    if [ -n "${SIM_FRAMEWORK_ARG}" ]; then
        xcodebuild -create-xcframework \
            -library "${DEV_STAGE}/${LIB}" ${DEV_HEADER_ARG} \
            ${SIM_FRAMEWORK_ARG} ${SIM_HEADER_ARG} \
            -output "${OUT_FRAMEWORK}"
    else
        xcodebuild -create-xcframework \
            -library "${DEV_STAGE}/${LIB}" ${DEV_HEADER_ARG} \
            -output "${OUT_FRAMEWORK}"
    fi
done

echo ""
echo "    iOS xcframeworks under: ${DIST_DIR}/"
ls -1 "${DIST_DIR}" | grep "\.xcframework$" || true
