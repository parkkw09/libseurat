#!/bin/bash
# =============================================================================
# macOS packaging: combine arm64 and x86_64 static archives into Universal
# Binaries via lipo, mirroring the directory layout of the input builds.
# =============================================================================

set -e

OUT_ROOT="$1"      # out/
DIST_DIR="$2"      # dist/osx
ARCHS="$3"         # "arm64 x86_64"

if [ -z "${OUT_ROOT}" ] || [ -z "${DIST_DIR}" ] || [ -z "${ARCHS}" ]; then
    echo "Usage: package-osx.sh <out_root> <dist_dir> <archs>" >&2
    exit 1
fi

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}/lib" "${DIST_DIR}/include"

PRIMARY=""
for ARCH in ${ARCHS}; do
    if [ -d "${OUT_ROOT}/${ARCH}/lib" ]; then
        PRIMARY="${ARCH}"
        break
    fi
done

if [ -z "${PRIMARY}" ]; then
    echo "ERROR: no built artifacts under ${OUT_ROOT}/{${ARCHS}}/lib" >&2
    exit 1
fi

# Headers from first available arch.
if [ -d "${OUT_ROOT}/${PRIMARY}/include" ]; then
    cp -r "${OUT_ROOT}/${PRIMARY}/include/." "${DIST_DIR}/include/"
fi

LIBS=$(find "${OUT_ROOT}/${PRIMARY}/lib" -maxdepth 1 -type f \( -name "*.a" -o -name "*.dylib" \) -exec basename {} \; | sort -u)

for LIB in ${LIBS}; do
    INPUTS=""
    for ARCH in ${ARCHS}; do
        CAND="${OUT_ROOT}/${ARCH}/lib/${LIB}"
        if [ -f "${CAND}" ]; then
            INPUTS="${INPUTS} ${CAND}"
        fi
    done

    if [ -z "${INPUTS}" ]; then
        continue
    fi

    # Count words (inputs) to decide between cp and lipo.
    COUNT=$(echo ${INPUTS} | wc -w | tr -d ' ')
    if [ "${COUNT}" = "1" ]; then
        cp ${INPUTS} "${DIST_DIR}/lib/${LIB}"
    else
        lipo -create ${INPUTS} -output "${DIST_DIR}/lib/${LIB}"
    fi
    echo "    -> ${DIST_DIR}/lib/${LIB}"
done

echo ""
echo "    macOS universal archives under: ${DIST_DIR}/"
