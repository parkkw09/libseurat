# =============================================================================
# iOS toolchain (64-bit only; arm64 device, arm64 simulator, legacy x86_64 sim)
# =============================================================================

if(NOT APPLE)
    message(FATAL_ERROR "iOS build is only available on macOS (Apple) hosts.")
endif()

message(STATUS "LEONARDO_ARCH = ${LEONARDO_ARCH}")

set(LEONARDO_IOS_DEPLOYMENT_TARGET "13.0" CACHE STRING "iOS minimum deployment target")

# When sub-projects (libyuv, SRT) re-enter this toolchain via `try_compile`,
# CMake launches a fresh CMake process that does NOT inherit parent cache vars.
# List our platform selectors here so they survive into TryCompile runs.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    LEONARDO_ARCH
    LEONARDO_TARGET
    LEONARDO_IOS_DEPLOYMENT_TARGET
    CMAKE_OSX_ARCHITECTURES
    CMAKE_OSX_SYSROOT
    CMAKE_OSX_DEPLOYMENT_TARGET)

# LEONARDO_ARCH mapping (64-bit only):
#   arm64      -> device (iphoneos)
#   arm64-sim  -> Apple Silicon iOS simulator
#   x86_64     -> legacy Intel iOS simulator
if(LEONARDO_ARCH STREQUAL "arm64")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
    set(IOS_PLATFORM "OS")
    set(IOS_SDK_NAME "iphoneos")
elseif(LEONARDO_ARCH STREQUAL "arm64-sim")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
    set(IOS_PLATFORM "Simulator")
    set(IOS_SDK_NAME "iphonesimulator")
elseif(LEONARDO_ARCH STREQUAL "x86_64")
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
    set(IOS_PLATFORM "Simulator")
    set(IOS_SDK_NAME "iphonesimulator")
else()
    message(FATAL_ERROR
        "Unsupported LEONARDO_ARCH = [${LEONARDO_ARCH}]. "
        "Supported (64-bit only): arm64, arm64-sim, x86_64.")
endif()

set(CMAKE_SYSTEM_NAME "iOS")
set(CMAKE_MACOSX_RPATH 1)

# Map LEONARDO_ARCH to a concrete CMAKE_SYSTEM_PROCESSOR so downstream projects
# (libyuv keys arch-specific sources off this) pick the correct sources.
if(LEONARDO_ARCH STREQUAL "arm64" OR LEONARDO_ARCH STREQUAL "arm64-sim")
    set(CMAKE_SYSTEM_PROCESSOR "arm64")
else()
    set(CMAKE_SYSTEM_PROCESSOR "${LEONARDO_ARCH}")
endif()

execute_process(COMMAND xcode-select -print-path
    RESULT_VARIABLE XCODE_SELECT_RESULT
    OUTPUT_VARIABLE XCODE_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT XCODE_SELECT_RESULT EQUAL 0)
    message(FATAL_ERROR "xcode-select failed (${XCODE_SELECT_RESULT}). Install Xcode.")
endif()

execute_process(COMMAND xcrun --sdk ${IOS_SDK_NAME} --show-sdk-version
    RESULT_VARIABLE XCRUN_RESULT
    OUTPUT_VARIABLE IOS_SDK_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT XCRUN_RESULT EQUAL 0)
    message(FATAL_ERROR "xcrun --show-sdk-version failed (${XCRUN_RESULT})")
endif()

execute_process(COMMAND xcrun --sdk ${IOS_SDK_NAME} --show-sdk-path
    RESULT_VARIABLE XCRUN_RESULT
    OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT XCRUN_RESULT EQUAL 0)
    message(FATAL_ERROR "xcrun --show-sdk-path failed (${XCRUN_RESULT})")
endif()

execute_process(COMMAND xcrun --sdk ${IOS_SDK_NAME} --show-sdk-platform-path
    RESULT_VARIABLE XCRUN_RESULT
    OUTPUT_VARIABLE CMAKE_OSX_PLATFORM_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT XCRUN_RESULT EQUAL 0)
    message(FATAL_ERROR "xcrun --show-sdk-platform-path failed (${XCRUN_RESULT})")
endif()

execute_process(COMMAND xcrun --sdk ${IOS_SDK_NAME} --find clang
    RESULT_VARIABLE XCRUN_RESULT
    OUTPUT_VARIABLE CLANG_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT XCRUN_RESULT EQUAL 0)
    message(FATAL_ERROR "xcrun --find clang failed (${XCRUN_RESULT})")
endif()
get_filename_component(TOOLCHAIN_PATH "${CLANG_PATH}" DIRECTORY)

message(STATUS "Using sysroot path  : ${CMAKE_OSX_SYSROOT}")
message(STATUS "Using platform path : ${CMAKE_OSX_PLATFORM_PATH}")
message(STATUS "Using sdk version   : ${IOS_SDK_VERSION}")
message(STATUS "Using toolchain path: ${TOOLCHAIN_PATH}")

set(TOOLCHAIN_CC     "${TOOLCHAIN_PATH}/clang")
set(TOOLCHAIN_CXX    "${TOOLCHAIN_PATH}/clang++")
set(TOOLCHAIN_LD     "${TOOLCHAIN_PATH}/ld")
set(TOOLCHAIN_AR     "${TOOLCHAIN_PATH}/ar")
set(TOOLCHAIN_RANLIB "${TOOLCHAIN_PATH}/ranlib")
set(TOOLCHAIN_STRIP  "${TOOLCHAIN_PATH}/strip")
set(TOOLCHAIN_NM     "${TOOLCHAIN_PATH}/nm")

set(TOOLCHAIN_CROSS_TOP        "${CMAKE_OSX_PLATFORM_PATH}/Developer")
set(TOOLCHAIN_CROSS_IPHONE_SDK "iPhone${IOS_PLATFORM}${IOS_SDK_VERSION}.sdk")

set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_SYSTEM_VERSION ${IOS_SDK_VERSION})
set(IOS TRUE)

# Modern iOS builds should NOT emit bitcode; Xcode 14+ dropped bitcode support.
if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO)
    set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO)
endif()

# Compiler (avoid removed CMakeForceCompiler).
set(CMAKE_C_COMPILER   "${TOOLCHAIN_CC}")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_CXX}")
set(CMAKE_AR      "${TOOLCHAIN_AR}"     CACHE FILEPATH "ar")
set(CMAKE_RANLIB  "${TOOLCHAIN_RANLIB}" CACHE FILEPATH "ranlib")
set(CMAKE_LINKER  "${TOOLCHAIN_LD}"     CACHE FILEPATH "linker")
set(CMAKE_NM      "${TOOLCHAIN_NM}"     CACHE FILEPATH "nm")

set(CMAKE_OSX_DEPLOYMENT_TARGET ${LEONARDO_IOS_DEPLOYMENT_TARGET})

set(CMAKE_FIND_ROOT_PATH ${CMAKE_OSX_SYSROOT} ${CMAKE_INSTALL_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

message(STATUS "CMAKE_HOST_SYSTEM_NAME  = [${CMAKE_HOST_SYSTEM_NAME}]")
message(STATUS "CMAKE_SYSTEM_NAME       = [${CMAKE_SYSTEM_NAME}]")
message(STATUS "CMAKE_SYSTEM_VERSION    = [${CMAKE_SYSTEM_VERSION}]")
message(STATUS "CMAKE_OSX_ARCHITECTURES = [${CMAKE_OSX_ARCHITECTURES}]")
message(STATUS "CMAKE_OSX_SYSROOT       = [${CMAKE_OSX_SYSROOT}]")
message(STATUS "IOS_PLATFORM            = [${IOS_PLATFORM}]")

list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_OSX_DEPLOYMENT_TARGET=${LEONARDO_IOS_DEPLOYMENT_TARGET}")

# Per-arch output dir uses LEONARDO_ARCH (not CMAKE_OSX_ARCHITECTURES) so
# arm64 device and arm64-sim outputs don't collide.
set(OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/out/${LEONARDO_ARCH})

list(APPEND COMMON_OPTIONS "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
list(APPEND COMMON_OPTIONS "-DCMAKE_PREFIX_PATH=${OUT_DIR}")
list(APPEND COMMON_OPTIONS "-DCMAKE_INSTALL_PREFIX=${OUT_DIR}")
list(APPEND COMMON_OPTIONS "-DCMAKE_FIND_ROOT_PATH=${OUT_DIR}")

message(STATUS "OUT_DIR              = ${OUT_DIR}")
message(STATUS "CMAKE_BUILD_TYPE     = ${CMAKE_BUILD_TYPE}")
