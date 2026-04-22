#!/bin/bash
# =============================================================================
# Android packaging: collect per-ABI static libs + headers into an AAR-style
# directory layout and zip it up as seurat.aar.
#
# Layout inside seurat.aar:
#   AndroidManifest.xml
#   jni/                     (headers — copied once; identical across ABIs)
#   jni/<abi>/               (static libs per ABI: arm64-v8a, x86_64)
#
# Note: a proper prefab-style AAR requires more metadata (prefab/*.json etc.),
# but this form is directly usable from an Android Gradle module by dropping
# the archive into `libs/` and referencing the unpacked `jni/` tree.
# =============================================================================

set -e

OUT_ROOT="$1"      # out/
DIST_DIR="$2"      # dist/android
ARCHS="$3"         # "arm64-v8a x86_64"

if [ -z "${OUT_ROOT}" ] || [ -z "${DIST_DIR}" ] || [ -z "${ARCHS}" ]; then
    echo "Usage: package-android.sh <out_root> <dist_dir> <archs>" >&2
    exit 1
fi

mkdir -p "${DIST_DIR}"
DIST_DIR="$(cd "${DIST_DIR}" && pwd)"
STAGE="${DIST_DIR}/_stage"
rm -rf "${STAGE}"
mkdir -p "${STAGE}/jni"

FIRST_ARCH=""
for ARCH in ${ARCHS}; do
    SRC="${OUT_ROOT}/${ARCH}"
    if [ ! -d "${SRC}" ]; then
        echo "WARN: ${SRC} not found, skipping ${ARCH}"
        continue
    fi
    FIRST_ARCH="${ARCH}"

    mkdir -p "${STAGE}/jni/${ARCH}"
    if [ -d "${SRC}/lib" ]; then
        find "${SRC}/lib" -maxdepth 1 -type f \( -name "*.a" -o -name "*.so" \) \
            -exec cp -v {} "${STAGE}/jni/${ARCH}/" \;
    fi
done

# Headers: copy once from the first ABI that built successfully.
if [ -n "${FIRST_ARCH}" ] && [ -d "${OUT_ROOT}/${FIRST_ARCH}/include" ]; then
    cp -r "${OUT_ROOT}/${FIRST_ARCH}/include" "${STAGE}/jni/"
fi

cat > "${STAGE}/AndroidManifest.xml" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.doubledragon.seurat" >
    <uses-sdk android:minSdkVersion="24" />
</manifest>
EOF

AAR="${DIST_DIR}/seurat.aar"
rm -f "${AAR}"
( cd "${STAGE}" && zip -r "${AAR}" . > /dev/null )

echo ""
echo "    Android AAR:  ${AAR}"
echo "    Unpacked tree: ${STAGE}"
