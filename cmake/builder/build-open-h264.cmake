# =============================================================================
# Cisco OpenH264 ExternalProject builder.
#
# OpenH264 uses a plain GNU Makefile (no configure, no CMake). We invoke
# `make` with platform-appropriate CC/CXX/CFLAGS/LDFLAGS injected as *env
# variables* via `cmake -E env` so that shell-quoting of multi-word values
# (xcrun invocations, multi-flag CFLAGS) stays intact.
#
# Installs libopenh264.a + headers under ${OUT_DIR}, matching the same
# layout that OpenSSL uses.
# =============================================================================

include(ExternalProject)
include(ProcessorCount)

set(OPEN_H264_VERSION "2.4.1" CACHE STRING "OpenH264 source tag")

ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 1)
endif()

set(OPEN_H264_OUT_DIR "${OUT_DIR}")

# Flags that vary by platform; populated below and then passed as env vars.
set(_oh_env_cc   "")
set(_oh_env_cxx  "")
set(_oh_env_ar   "")
set(_oh_env_ranlib "")
set(_oh_env_cflags "")
set(_oh_env_ldflags "")

# Per-platform `make` keyword arguments (OS=..., ARCH=..., SDK=..., etc).
set(_oh_make_args "PREFIX=${OPEN_H264_OUT_DIR}")

if(ANDROID)
    if(SEURAT_ARCH STREQUAL "arm64-v8a")
        set(_oh_arch "arm64")
    elseif(SEURAT_ARCH STREQUAL "x86_64")
        set(_oh_arch "x86_64")
    else()
        message(FATAL_ERROR "OpenH264: unsupported Android arch [${SEURAT_ARCH}]")
    endif()

    list(APPEND _oh_make_args
        "OS=android"
        "ARCH=${_oh_arch}"
        "NDKROOT=${CMAKE_ANDROID_NDK}"
        "TARGET=android-${SEURAT_ANDROID_MIN_API}"
        "TOOLCHAINPREFIX=${ANDROID_TOOLCHAIN_BIN}/llvm-")

    set(_oh_env_cc     "${ANDROID_TOOLCHAIN_BIN}/${ANDROID_TARGET_HOST_API}-clang")
    set(_oh_env_cxx    "${ANDROID_TOOLCHAIN_BIN}/${ANDROID_TARGET_HOST_API}-clang++")
    set(_oh_env_ar     "${ANDROID_TOOLCHAIN_BIN}/llvm-ar")
    set(_oh_env_ranlib "${ANDROID_TOOLCHAIN_BIN}/llvm-ranlib")

elseif(IOS)
    if(SEURAT_ARCH STREQUAL "arm64")
        set(_oh_arch "arm64")
        set(_oh_sdk  "iphoneos")
        set(_oh_min_flag "-miphoneos-version-min=${SEURAT_IOS_DEPLOYMENT_TARGET}")
    elseif(SEURAT_ARCH STREQUAL "arm64-sim")
        set(_oh_arch "arm64")
        set(_oh_sdk  "iphonesimulator")
        set(_oh_min_flag "-mios-simulator-version-min=${SEURAT_IOS_DEPLOYMENT_TARGET}")
    elseif(SEURAT_ARCH STREQUAL "x86_64")
        set(_oh_arch "x86_64")
        set(_oh_sdk  "iphonesimulator")
        set(_oh_min_flag "-mios-simulator-version-min=${SEURAT_IOS_DEPLOYMENT_TARGET}")
    else()
        message(FATAL_ERROR "OpenH264: unsupported iOS arch [${SEURAT_ARCH}]")
    endif()

    list(APPEND _oh_make_args
        "OS=ios"
        "ARCH=${_oh_arch}"
        "SDK=${_oh_sdk}"
        "SDK_MIN=${SEURAT_IOS_DEPLOYMENT_TARGET}")

    set(_oh_env_cc      "xcrun -sdk ${_oh_sdk} clang")
    set(_oh_env_cxx     "xcrun -sdk ${_oh_sdk} clang++")
    set(_oh_env_cflags  "-arch ${_oh_arch} ${_oh_min_flag}")
    set(_oh_env_ldflags "-arch ${_oh_arch} ${_oh_min_flag}")

elseif(APPLE)
    if(NOT SEURAT_ARCH MATCHES "^(arm64|x86_64)$")
        message(FATAL_ERROR "OpenH264: unsupported OSX arch [${SEURAT_ARCH}]")
    endif()

    list(APPEND _oh_make_args
        "OS=darwin"
        "ARCH=${SEURAT_ARCH}")

    set(_oh_env_cc      "clang -arch ${SEURAT_ARCH} -mmacosx-version-min=${SEURAT_OSX_DEPLOYMENT_TARGET}")
    set(_oh_env_cxx     "clang++ -arch ${SEURAT_ARCH} -mmacosx-version-min=${SEURAT_OSX_DEPLOYMENT_TARGET}")
    set(_oh_env_cflags  "-arch ${SEURAT_ARCH} -mmacosx-version-min=${SEURAT_OSX_DEPLOYMENT_TARGET}")
    set(_oh_env_ldflags "-arch ${SEURAT_ARCH} -mmacosx-version-min=${SEURAT_OSX_DEPLOYMENT_TARGET}")

else()
    message(FATAL_ERROR "OpenH264: target [${SEURAT_TARGET}] not supported")
endif()

# Build the `cmake -E env ...` prefix, only emitting vars we actually set.
set(_oh_env_prefix ${CMAKE_COMMAND} -E env)
if(_oh_env_cc)
    list(APPEND _oh_env_prefix "CC=${_oh_env_cc}")
endif()
if(_oh_env_cxx)
    list(APPEND _oh_env_prefix "CXX=${_oh_env_cxx}")
endif()
if(_oh_env_ar)
    list(APPEND _oh_env_prefix "AR=${_oh_env_ar}")
endif()
if(_oh_env_ranlib)
    list(APPEND _oh_env_prefix "RANLIB=${_oh_env_ranlib}")
endif()
if(_oh_env_cflags)
    list(APPEND _oh_env_prefix "CFLAGS=${_oh_env_cflags}")
    list(APPEND _oh_env_prefix "CXXFLAGS=${_oh_env_cflags}")
endif()
if(_oh_env_ldflags)
    list(APPEND _oh_env_prefix "LDFLAGS=${_oh_env_ldflags}")
endif()

message(STATUS "OPEN_H264_VERSION   = ${OPEN_H264_VERSION}")
message(STATUS "OPEN_H264_MAKE_ARGS = ${_oh_make_args}")
message(STATUS "OPEN_H264_ENV       = ${_oh_env_prefix}")

# PATCH_COMMAND: drop in our sanitised platform-ios.mk. No-op for non-iOS
# builds (the file just isn't referenced). Using `cmake -E copy` keeps this
# cross-platform (BSD sed / GNU sed / etc).
set(_oh_platform_ios_src "${CMAKE_CURRENT_LIST_DIR}/openh264/platform-ios.mk")

ExternalProject_Add(openh264
    PREFIX          openh264
    BUILD_IN_SOURCE 1
    GIT_REPOSITORY  "https://github.com/cisco/openh264.git"
    GIT_TAG         "v${OPEN_H264_VERSION}"
    GIT_SHALLOW     TRUE
    UPDATE_COMMAND  ""
    PATCH_COMMAND
        ${CMAKE_COMMAND} -E copy "${_oh_platform_ios_src}"
            "<SOURCE_DIR>/build/platform-ios.mk"
    CONFIGURE_COMMAND ""
    # Build only the static archive. OpenH264's default `all` target also
    # builds the shared lib and demo APKs, the latter of which invokes
    # ndk-build + gradle on Android and fails in containerised envs. We only
    # ship libopenh264.a anyway.
    BUILD_COMMAND     ${_oh_env_prefix} make ${_oh_make_args} libopenh264.a -j${NPROC}
    INSTALL_COMMAND   ${_oh_env_prefix} make ${_oh_make_args} install-static)
