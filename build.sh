#!/bin/bash
# =============================================================================
# libseurat (leonardo) build driver — 64-bit only
#
# Usage:
#   ./build.sh ANDROID [Release|Debug|Dev]        # arm64-v8a + x86_64
#   ./build.sh IOS     [Release|Debug|Dev]        # arm64 device + arm64-sim + x86_64 sim
#   ./build.sh OSX     [Release|Debug|Dev]        # arm64 + x86_64 (Universal)
#   ./build.sh MSVC    [Release|Debug|Dev]        # x86_64
#
#   After building, distributable packages are produced under dist/:
#     Android -> dist/android/seurat.aar  (static archives + headers in jniLibs/jni)
#     iOS     -> dist/ios/Seurat.xcframework (arm64 device + simulator slices)
#     OSX     -> dist/osx/                  (universal binaries)
#
# Environment:
#   ANDROID_NDK_HOME  (or ANDROID_NDK_ROOT / ANDROID_NDK / NDK_R26)
#                     must point to NDK r21 or later (r26+ recommended).
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}"

TMP=WORK
OUT=out
DIST=dist
TARGET=""
BUILD_TYPE="Release"
BUILD_OPTION=""
ARCHS=""

if [ "$1" = "ANDROID" ]; then
    echo ">>> ANDROID build (64-bit only)"
    TARGET="ANDROID"
    ARCHS="arm64-v8a x86_64"
elif [ "$1" = "IOS" ]; then
    echo ">>> IOS build (64-bit only)"
    TARGET="IOS"
    # arm64        = iphoneos device
    # arm64-sim    = Apple Silicon iphonesimulator
    # x86_64       = legacy Intel iphonesimulator (optional; drop if you don't need it)
    ARCHS="arm64 arm64-sim x86_64"
elif [ "$1" = "OSX" ]; then
    echo ">>> OSX build (64-bit only)"
    TARGET="OSX"
    ARCHS="arm64 x86_64"
elif [ "$1" = "MSVC" ]; then
    echo ">>> MSVC build (64-bit only)"
    TARGET="MSVC"
    ARCHS="x86_64"
else
    echo "Usage: $0 {ANDROID|IOS|OSX|MSVC} [Release|Debug|Dev]"
    exit 1
fi

case "$2" in
    DEBUG|Debug|debug)
        echo "    build type DEBUG"
        BUILD_TYPE="Debug"
        BUILD_OPTION="VERBOSE=1"
        ;;
    DEV|Dev|dev)
        echo "    build type DEV (single-arch)"
        BUILD_TYPE="Release"
        BUILD_OPTION="VERBOSE=1"
        # Pick a sensible single-arch default per target for fast iteration.
        case "${TARGET}" in
            ANDROID) ARCHS="arm64-v8a" ;;
            IOS)     ARCHS="arm64-sim" ;;
            OSX)     ARCHS="arm64" ;;
            MSVC)    ARCHS="x86_64" ;;
        esac
        ;;
    *)
        echo "    build type RELEASE"
        BUILD_TYPE="Release"
        BUILD_OPTION=""
        ;;
esac

mkdir -p "${TMP}" "${DIST}"

# -----------------------------------------------------------------------------
# Per-arch build loop.
# -----------------------------------------------------------------------------
for ARCH in ${ARCHS}; do
    echo "====================================================================="
    echo "  Building ${TARGET} / ${ARCH} (${BUILD_TYPE})"
    echo "====================================================================="

    BUILD_DIR="${TMP}/${TARGET}-${ARCH}"
    mkdir -p "${BUILD_DIR}"

    cmake -S . -B "${BUILD_DIR}" \
        -D LEONARDO_ARCH=${ARCH} \
        -D LEONARDO_TARGET=${TARGET} \
        -D LEONARDO_CRYPTO=ON \
        -D CMAKE_BUILD_TYPE=${BUILD_TYPE}

    cmake --build "${BUILD_DIR}" -- ${BUILD_OPTION}
done

# -----------------------------------------------------------------------------
# Packaging.
# -----------------------------------------------------------------------------
case "${TARGET}" in
    ANDROID)
        bash "${SCRIPT_DIR}/scripts/package-android.sh" "${OUT}" "${DIST}/android" "${ARCHS}"
        ;;
    IOS)
        bash "${SCRIPT_DIR}/scripts/package-ios.sh" "${OUT}" "${DIST}/ios" "${ARCHS}"
        ;;
    OSX)
        bash "${SCRIPT_DIR}/scripts/package-osx.sh" "${OUT}" "${DIST}/osx" "${ARCHS}"
        ;;
    MSVC)
        echo "    (MSVC packaging step not yet implemented)"
        ;;
esac

echo ""
echo ">>> Build complete. Artifacts under: ${DIST}/"
