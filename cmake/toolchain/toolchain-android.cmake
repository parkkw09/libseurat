# =============================================================================
# Android toolchain (NDK r21+ unified sysroot, Clang, 64-bit ABIs only)
#
# NDK discovery order:
#   1. ANDROID_NDK_HOME
#   2. ANDROID_NDK_ROOT
#   3. ANDROID_NDK
#   4. NDK_R26 (back-compat with build env)
#   5. NDK_R13 (legacy, deprecated)
#   6. ndk-build found in PATH
# =============================================================================

set(LEONARDO_ANDROID_MIN_API 24 CACHE STRING "Android minimum API level (64-bit only, default 24)")
set(LEONARDO_ANDROID_STL "c++_static" CACHE STRING "Android STL type (c++_static or c++_shared)")

foreach(_ndk_env IN ITEMS ANDROID_NDK_HOME ANDROID_NDK_ROOT ANDROID_NDK NDK_R26 NDK_R25 NDK_R24 NDK_R23 NDK_R22 NDK_R21 NDK_R13)
    if(NOT CMAKE_ANDROID_NDK AND DEFINED ENV{${_ndk_env}})
        set(CMAKE_ANDROID_NDK "$ENV{${_ndk_env}}")
        message(STATUS "Found Android NDK via ${_ndk_env} = ${CMAKE_ANDROID_NDK}")
    endif()
endforeach()

if(NOT CMAKE_ANDROID_NDK)
    find_program(ANDROID_NDK_BUILD_PROGRAM ndk-build)
    if(NOT ANDROID_NDK_BUILD_PROGRAM)
        message(FATAL_ERROR
            "Android NDK not found. Set one of:"
            " ANDROID_NDK_HOME / ANDROID_NDK_ROOT / ANDROID_NDK"
            " (NDK r21+ recommended, r26 preferred)")
    endif()
    get_filename_component(CMAKE_ANDROID_NDK "${ANDROID_NDK_BUILD_PROGRAM}" DIRECTORY)
endif()

# Detect NDK source.properties to validate version.
set(_ndk_source_props "${CMAKE_ANDROID_NDK}/source.properties")
if(EXISTS "${_ndk_source_props}")
    file(READ "${_ndk_source_props}" _ndk_props_text)
    string(REGEX MATCH "Pkg.Revision = ([0-9]+)\\.([0-9]+)\\.([0-9]+)"
           _ndk_ver_match "${_ndk_props_text}")
    set(NDK_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(NDK_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(NDK_VERSION_PATCH "${CMAKE_MATCH_3}")
    message(STATUS "Android NDK version: ${NDK_VERSION_MAJOR}.${NDK_VERSION_MINOR}.${NDK_VERSION_PATCH}")
    if(NDK_VERSION_MAJOR LESS 21)
        message(FATAL_ERROR
            "Android NDK r${NDK_VERSION_MAJOR} is not supported. "
            "Please install NDK r21 or later (r26 recommended).")
    endif()
endif()

message(STATUS "LEONARDO_ARCH = ${LEONARDO_ARCH}")

set(CMAKE_SYSTEM_NAME "Android")
set(CMAKE_ANDROID_STL_TYPE "${LEONARDO_ANDROID_STL}")
set(CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION "clang")

# 64-bit ABIs only.
if(LEONARDO_ARCH STREQUAL "arm64-v8a")
    set(CMAKE_ANDROID_ARCH "arm64")
    set(CMAKE_ANDROID_ARCH_ABI "arm64-v8a")
    set(ANDROID_LLVM_TRIPLE "aarch64-linux-android")
elseif(LEONARDO_ARCH STREQUAL "x86_64")
    set(CMAKE_ANDROID_ARCH "x86_64")
    set(CMAKE_ANDROID_ARCH_ABI "x86_64")
    set(ANDROID_LLVM_TRIPLE "x86_64-linux-android")
else()
    message(FATAL_ERROR
        "Unsupported LEONARDO_ARCH = [${LEONARDO_ARCH}]. "
        "64-bit only build supports: arm64-v8a, x86_64.")
endif()

set(CMAKE_SYSTEM_VERSION "${LEONARDO_ANDROID_MIN_API}")
set(CMAKE_ANDROID_API "${LEONARDO_ANDROID_MIN_API}")

# Host tag
if(CMAKE_HOST_SYSTEM_NAME STREQUAL Linux)
    set(ANDROID_HOST_TAG linux-x86_64)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL Darwin)
    set(ANDROID_HOST_TAG darwin-x86_64)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL Windows)
    set(ANDROID_HOST_TAG windows-x86_64)
endif()

# Modern unified LLVM toolchain directory.
set(ANDROID_TOOLCHAIN_ROOT "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/${ANDROID_HOST_TAG}")
set(ANDROID_TOOLCHAIN_BIN  "${ANDROID_TOOLCHAIN_ROOT}/bin")
set(ANDROID_SYSROOT_ROOT   "${ANDROID_TOOLCHAIN_ROOT}/sysroot")

if(NOT EXISTS "${ANDROID_TOOLCHAIN_BIN}")
    message(FATAL_ERROR
        "Expected modern NDK toolchain at ${ANDROID_TOOLCHAIN_BIN} does not exist.\n"
        "Please upgrade to NDK r21 or later (r26 recommended).")
endif()

# api-level-specific clang wrappers provided by NDK.
set(ANDROID_TARGET_HOST     "${ANDROID_LLVM_TRIPLE}")
set(ANDROID_TARGET_HOST_API "${ANDROID_LLVM_TRIPLE}${LEONARDO_ANDROID_MIN_API}")

# Cross-compile prefix used by configure-based libraries (openssl, etc.).
# Picks up llvm-ar, llvm-ranlib, llvm-strip, llvm-nm under the llvm/ toolchain.
set(CMAKE_ANDROID_TOOLCHAIN_MACHINE "${ANDROID_LLVM_TRIPLE}")
set(CMAKE_CXX_ANDROID_TOOLCHAIN_MACHINE "${ANDROID_LLVM_TRIPLE}")
set(CMAKE_C_ANDROID_TOOLCHAIN_PREFIX  "${ANDROID_TOOLCHAIN_BIN}/llvm-")
set(CMAKE_CXX_ANDROID_TOOLCHAIN_PREFIX "${ANDROID_TOOLCHAIN_BIN}/llvm-")

message(STATUS "CMAKE_HOST_SYSTEM_NAME  = [${CMAKE_HOST_SYSTEM_NAME}]")
message(STATUS "CMAKE_SYSTEM_NAME       = [${CMAKE_SYSTEM_NAME}]")
message(STATUS "CMAKE_SYSTEM_VERSION    = [${CMAKE_SYSTEM_VERSION}]")
message(STATUS "CMAKE_ANDROID_API       = [${CMAKE_ANDROID_API}]")
message(STATUS "CMAKE_ANDROID_ARCH      = [${CMAKE_ANDROID_ARCH}]")
message(STATUS "CMAKE_ANDROID_ARCH_ABI  = [${CMAKE_ANDROID_ARCH_ABI}]")
message(STATUS "CMAKE_ANDROID_STL_TYPE  = [${CMAKE_ANDROID_STL_TYPE}]")
message(STATUS "CMAKE_ANDROID_NDK       = [${CMAKE_ANDROID_NDK}]")
message(STATUS "ANDROID_TOOLCHAIN_ROOT  = ${ANDROID_TOOLCHAIN_ROOT}")
message(STATUS "ANDROID_SYSROOT_ROOT    = ${ANDROID_SYSROOT_ROOT}")
message(STATUS "ANDROID_HOST_TAG        = ${ANDROID_HOST_TAG}")
message(STATUS "ANDROID_LLVM_TRIPLE     = ${ANDROID_LLVM_TRIPLE}")
message(STATUS "ANDROID_TARGET_HOST_API = ${ANDROID_TARGET_HOST_API}")

list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_ANDROID_ARCH_ABI=${CMAKE_ANDROID_ARCH_ABI}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_ANDROID_NDK=${CMAKE_ANDROID_NDK}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_ANDROID_STL_TYPE=${CMAKE_ANDROID_STL_TYPE}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_ANDROID_NDK_TOOLCHAIN_VERSION=clang")

set(OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/out/${CMAKE_ANDROID_ARCH_ABI})

list(APPEND COMMON_OPTIONS "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
list(APPEND COMMON_OPTIONS "-DCMAKE_PREFIX_PATH=${OUT_DIR}")
list(APPEND COMMON_OPTIONS "-DCMAKE_INSTALL_PREFIX=${OUT_DIR}")

message(STATUS "OUT_DIR              = ${OUT_DIR}")
message(STATUS "CMAKE_BUILD_TYPE     = ${CMAKE_BUILD_TYPE}")
