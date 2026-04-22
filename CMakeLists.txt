cmake_minimum_required(VERSION 3.21)

# -----------------------------------------------------------------------------
# External dependencies (ExternalProject-based, always built into OUT_DIR).
# -----------------------------------------------------------------------------
option(LEONARDO_CRYPTO   "LEONARDO Support Crypto Algorithm (OpenSSL)."  ON)
option(LEONARDO_OPENH264 "LEONARDO Support Cisco OpenH264 codec."        ON)
option(LEONARDO_YUV      "LEONARDO Support libyuv (YUV colorspace ops)." ON)
option(LEONARDO_SRT      "LEONARDO Support SRT transport."               ON)

# -----------------------------------------------------------------------------
# Internal seurat components (live under src/, built as part of this project).
#
# NOTE: we used to wire `LEONARDO_RTMP` to librtmp (rtmpdump, LGPL + no RTMPS,
# unmaintained since 2015). docs/DESIGN.md §3.4 rejects that path; instead we ship
# our own RTMP(S) client + FLV muxer (MIT-licensed internal code).
# -----------------------------------------------------------------------------
option(LEONARDO_FLV      "Build seurat-flv (FLV muxer, internal, MIT)."  ON)
option(LEONARDO_RTMP     "Build seurat-rtmp (RTMP(S) publisher, MIT)."   ON)

# -----------------------------------------------------------------------------
# Still-pending experimental components (kept OFF until designed/implemented).
# -----------------------------------------------------------------------------
option(LEONARDO_RTC      "LEONARDO Support WebRTC (experimental)."       OFF)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake" "${CMAKE_MODULE_PATH}")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/builder" "${CMAKE_MODULE_PATH}")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain" "${CMAKE_MODULE_PATH}")

# 64-bit only build targets.
# Allowed LEONARDO_ARCH values:
#   ANDROID -> arm64-v8a, x86_64
#   IOS     -> arm64 (device), arm64-sim (simulator arm64), x86_64 (simulator intel, legacy)
#   OSX     -> arm64, x86_64
#   MSVC    -> x86_64
set(LEONARDO_TARGET "ANDROID" CACHE STRING "Build target platform: ANDROID, IOS, MSVC, OSX")
set(LEONARDO_ARCH "arm64-v8a" CACHE STRING "Target architecture (64-bit only)")

if(LEONARDO_TARGET STREQUAL "ANDROID")
    include(toolchain-android)
elseif(LEONARDO_TARGET STREQUAL "IOS")
    include(toolchain-ios)
elseif(LEONARDO_TARGET STREQUAL "MSVC")
    include(toolchain-msvc)
elseif(LEONARDO_TARGET STREQUAL "OSX")
    include(toolchain-osx)
else()
    message(FATAL_ERROR "Unknown LEONARDO_TARGET = [${LEONARDO_TARGET}]")
endif()

project(LEONARDO VERSION 0.2.0 LANGUAGES C CXX)

include(toolchain-common)

message(STATUS "Compiler")
message(STATUS " - System   \t: ${CMAKE_SYSTEM_NAME}")
message(STATUS " - ID       \t: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS " - Version  \t: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS " - CXX Path \t: ${CMAKE_CXX_COMPILER}")
message(STATUS " - C Path   \t: ${CMAKE_C_COMPILER}")

# -----------------------------------------------------------------------------
# External dependencies.
# -----------------------------------------------------------------------------
if(LEONARDO_CRYPTO)
    include(build-openssl)
endif()

if(LEONARDO_OPENH264)
    include(build-open-h264)
endif()

if(LEONARDO_YUV)
    include(build-yuv)
endif()

if(LEONARDO_SRT)
    include(build-srt)
endif()

if(LEONARDO_RTC)
    include(build-rtc)
endif()

# -----------------------------------------------------------------------------
# Internal seurat components.
#
# Declared AFTER external deps so RTMP (which links against OpenSSL) can
# observe OUT_DIR/lib/libssl.a etc. once they exist.
# -----------------------------------------------------------------------------
if(LEONARDO_FLV)
    add_subdirectory(src/seurat-flv)
endif()

if(LEONARDO_RTMP)
    if(NOT LEONARDO_FLV)
        message(FATAL_ERROR
            "LEONARDO_RTMP=ON requires LEONARDO_FLV=ON (RTMP publisher "
            "depends on the FLV muxer for payload framing).")
    endif()
    add_subdirectory(src/seurat-rtmp)
endif()
