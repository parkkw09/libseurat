# =============================================================================
# OpenSSL 3.x ExternalProject builder (64-bit only).
# =============================================================================

include(ExternalProject)
include(ProcessorCount)

set(OPENSSL_BUILDER "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builder/openssl")
set(CMAKE_MODULE_PATH "${OPENSSL_BUILDER}" "${CMAKE_MODULE_PATH}")
set(BUILDER_NAME "openssl")

# Pinned to a modern LTS-style release.
set(OPENSSL_BUILD_VERSION "3.3.2" CACHE STRING "OpenSSL source version")

set(OPENSSL_ARCH "")
set(OPENSSL_PRE_BUILD "")
set(OPENSSL_OUT_DIR ${OUT_DIR})

# 3.x uses "no-shared" (keep) and drops many legacy protocols; keep things minimal.
set(OPENSSL_COMMON_NO
    no-shared no-tests no-docs no-apps
    no-legacy no-ssl3 no-weak-ssl-ciphers)

list(APPEND OPENSSL_CONFIGURE_OPTIONS ${OPENSSL_COMMON_NO})
list(APPEND OPENSSL_CONFIGURE_OPTIONS "--prefix=${OPENSSL_OUT_DIR}" "--openssldir=${OPENSSL_OUT_DIR}/ssl")

ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 1)
endif()

if(ANDROID)
    # OpenSSL 3.x Android targets:
    #   android-arm64  / android-x86_64  / android-arm  / android-x86
    if(LEONARDO_ARCH STREQUAL "arm64-v8a")
        set(OPENSSL_ARCH "android-arm64")
    elseif(LEONARDO_ARCH STREQUAL "x86_64")
        set(OPENSSL_ARCH "android-x86_64")
    else()
        message(FATAL_ERROR "OpenSSL: unsupported Android arch [${LEONARDO_ARCH}]")
    endif()

    set(OPENSSL_PRE_BUILD
        ${OPENSSL_BUILDER}/pre_build_android.sh
        -t ${ANDROID_TOOLCHAIN_ROOT}
        -h ${ANDROID_TARGET_HOST}
        -a ${ANDROID_TARGET_HOST_API})

    # OpenSSL 3.x android-* targets use ANDROID_NDK_ROOT for sysroot resolution.
    list(APPEND OPENSSL_CONFIGURE_OPTIONS "-D__ANDROID_API__=${LEONARDO_ANDROID_MIN_API}")
    list(APPEND OPENSSL_CONFIGURE_OPTIONS "-fPIC")

elseif(IOS)
    # OpenSSL's ios*-xcrun targets internally use `xcrun -sdk <iphoneos|
    # iphonesimulator> cc` as the compiler, which transparently supplies
    # the right sysroot and min-version. We must NOT override CC via a
    # pre-build script, or the xcrun hookup breaks and headers like
    # <stdlib.h>/<assert.h> disappear.
    set(_openssl_cc_override "")
    if(LEONARDO_ARCH STREQUAL "arm64")
        set(OPENSSL_ARCH "ios64-xcrun")
    elseif(LEONARDO_ARCH STREQUAL "arm64-sim")
        set(OPENSSL_ARCH "iossimulator-arm64-xcrun")
    elseif(LEONARDO_ARCH STREQUAL "x86_64")
        # iossimulator-xcrun has no baked-in -arch, so on Apple Silicon hosts
        # it defaults to arm64. Override CC to force x86_64.
        set(OPENSSL_ARCH "iossimulator-xcrun")
        set(_openssl_cc_override "xcrun -sdk iphonesimulator cc -arch x86_64")
    else()
        message(FATAL_ERROR "OpenSSL: unsupported iOS arch [${LEONARDO_ARCH}]")
    endif()

    if(_openssl_cc_override)
        # OpenSSL reads CC from env and overrides the target's default.
        set(OPENSSL_PRE_BUILD
            ${OPENSSL_BUILDER}/pre_build_ios.sh
            -c ${_openssl_cc_override})
    else()
        set(OPENSSL_PRE_BUILD "")
    endif()
    list(APPEND OPENSSL_CONFIGURE_OPTIONS "-fPIC")
    # Note: OpenSSL's ios*-xcrun targets bake in a default min-version (12.0).
    # Overriding requires picking -mios-version-min / -mios-simulator-version-min
    # based on platform, which is messy, so we accept the upstream default here.

elseif(APPLE)
    # macOS native (no pre-build wrapper needed — Configure handles host detection).
    if(LEONARDO_ARCH STREQUAL "arm64")
        set(OPENSSL_ARCH "darwin64-arm64-cc")
    elseif(LEONARDO_ARCH STREQUAL "x86_64")
        set(OPENSSL_ARCH "darwin64-x86_64-cc")
    else()
        message(FATAL_ERROR "OpenSSL: unsupported macOS arch [${LEONARDO_ARCH}]")
    endif()

elseif(WIN32)
    set(OPENSSL_ARCH "VC-WIN64A")
    message(WARNING "OpenSSL Windows build via ExternalProject requires Perl + NASM in PATH.")
else()
    message(FATAL_ERROR "OpenSSL: unknown target platform")
endif()

message(STATUS "OPENSSL_BUILD_VERSION    = ${OPENSSL_BUILD_VERSION}")
message(STATUS "OPENSSL_ARCH             = ${OPENSSL_ARCH}")
message(STATUS "OPENSSL_PRE_BUILD        = ${OPENSSL_PRE_BUILD}")
message(STATUS "OPENSSL_CONFIGURE_OPTIONS= ${OPENSSL_CONFIGURE_OPTIONS}")
message(STATUS "OPENSSL_OUT_DIR          = ${OPENSSL_OUT_DIR}")

# Build command composition differs based on whether we have a pre-build script.
if(OPENSSL_PRE_BUILD)
    set(_configure_cmd source ${OPENSSL_PRE_BUILD} && ./Configure ${OPENSSL_ARCH} ${OPENSSL_CONFIGURE_OPTIONS})
    set(_build_cmd     source ${OPENSSL_PRE_BUILD} && make -j${NPROC})
    set(_install_cmd   source ${OPENSSL_PRE_BUILD} && make install_sw)
else()
    set(_configure_cmd ./Configure ${OPENSSL_ARCH} ${OPENSSL_CONFIGURE_OPTIONS})
    set(_build_cmd     make -j${NPROC})
    set(_install_cmd   make install_sw)
endif()

ExternalProject_Add(${BUILDER_NAME}
    PREFIX ${BUILDER_NAME}
    BUILD_IN_SOURCE 1
    URL      https://www.openssl.org/source/openssl-${OPENSSL_BUILD_VERSION}.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ${_configure_cmd}
    BUILD_COMMAND     ${_build_cmd}
    INSTALL_COMMAND   ${_install_cmd}
)
