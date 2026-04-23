cmake_minimum_required(VERSION 3.21)

# -----------------------------------------------------------------------------
# External dependencies (ExternalProject-based, always built into OUT_DIR).
# -----------------------------------------------------------------------------
option(SEURAT_CRYPTO   "SEURAT Support Crypto Algorithm (OpenSSL)."  ON)
option(SEURAT_OPENH264 "SEURAT Support Cisco OpenH264 codec."        OFF)
option(SEURAT_YUV      "SEURAT Support libyuv (YUV colorspace ops)." ON)
option(SEURAT_SRT      "SEURAT Support SRT transport."               ON)

# -----------------------------------------------------------------------------
# Internal seurat components (live under src/, built as part of this project).
#
# NOTE: we used to wire `SEURAT_RTMP` to librtmp (rtmpdump, LGPL + no RTMPS,
# unmaintained since 2015). docs/DESIGN.md §3.4 rejects that path; instead we ship
# our own RTMP(S) client + FLV muxer (MIT-licensed internal code).
# -----------------------------------------------------------------------------
option(SEURAT_FLV      "Build seurat-flv (FLV muxer, internal, MIT)."  ON)
option(SEURAT_RTMP     "Build seurat-rtmp (RTMP(S) publisher, MIT)."   ON)
option(SEURAT_H264_NATIVE "Build seurat-h264-native (Platform H.264 wrapper, MIT)." ON)
option(SEURAT_AAC_NATIVE  "Build seurat-aac-native (Platform AAC wrapper, MIT)."    ON)

# -----------------------------------------------------------------------------
# Still-pending experimental components (kept OFF until designed/implemented).
# -----------------------------------------------------------------------------
option(SEURAT_RTC      "SEURAT Support WebRTC (experimental)."       OFF)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake" "${CMAKE_MODULE_PATH}")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/builder" "${CMAKE_MODULE_PATH}")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain" "${CMAKE_MODULE_PATH}")

# 64-bit only build targets.
# Allowed SEURAT_ARCH values:
#   ANDROID -> arm64-v8a, x86_64
#   IOS     -> arm64 (device), arm64-sim (simulator arm64), x86_64 (simulator intel, legacy)
#   OSX     -> arm64, x86_64
#   MSVC    -> x86_64
set(SEURAT_TARGET "ANDROID" CACHE STRING "Build target platform: ANDROID, IOS, MSVC, OSX")
set(SEURAT_ARCH "arm64-v8a" CACHE STRING "Target architecture (64-bit only)")

if(SEURAT_TARGET STREQUAL "ANDROID")
    include(toolchain-android)
elseif(SEURAT_TARGET STREQUAL "IOS")
    include(toolchain-ios)
elseif(SEURAT_TARGET STREQUAL "MSVC")
    include(toolchain-msvc)
elseif(SEURAT_TARGET STREQUAL "OSX")
    include(toolchain-osx)
else()
    message(FATAL_ERROR "Unknown SEURAT_TARGET = [${SEURAT_TARGET}]")
endif()

project(SEURAT VERSION 0.2.0 LANGUAGES C CXX)

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
if(SEURAT_CRYPTO)
    include(build-openssl)
endif()

if(SEURAT_OPENH264)
    include(build-open-h264)
endif()

if(SEURAT_YUV)
    include(build-yuv)
endif()

if(SEURAT_SRT)
    include(build-srt)
endif()

if(SEURAT_RTC)
    include(build-rtc)
endif()

# -----------------------------------------------------------------------------
# Internal seurat components.
#
# Declared AFTER external deps so RTMP (which links against OpenSSL) can
# observe OUT_DIR/lib/libssl.a etc. once they exist.
# -----------------------------------------------------------------------------
if(SEURAT_FLV)
    add_subdirectory(src/seurat-flv)
endif()

if(SEURAT_RTMP)
    if(NOT SEURAT_FLV)
        message(FATAL_ERROR
            "SEURAT_RTMP=ON requires SEURAT_FLV=ON (RTMP publisher "
            "depends on the FLV muxer for payload framing).")
    endif()
    add_subdirectory(src/seurat-rtmp)
endif()

if(SEURAT_H264_NATIVE)
    add_subdirectory(src/seurat-h264-native)
endif()

if(SEURAT_AAC_NATIVE)
    add_subdirectory(src/seurat-aac-native)
endif()

# -----------------------------------------------------------------------------
# Final Shared Library Packaging
# Combine all internal targets and external static archives into a single dynamic library
# -----------------------------------------------------------------------------
add_library(seurat SHARED src/seurat_dummy.c)
set_target_properties(seurat PROPERTIES 
    LINKER_LANGUAGE CXX
    LIBRARY_OUTPUT_DIRECTORY "${OUT_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${OUT_DIR}/lib" # For .so on some platforms
)

# Create a list of internal targets we built
set(SEURAT_INTERNAL_TARGETS "")
if(SEURAT_FLV)
    list(APPEND SEURAT_INTERNAL_TARGETS seurat-flv)
endif()
if(SEURAT_RTMP)
    list(APPEND SEURAT_INTERNAL_TARGETS seurat-rtmp)
endif()
if(SEURAT_H264_NATIVE)
    list(APPEND SEURAT_INTERNAL_TARGETS seurat-h264-native)
endif()
if(SEURAT_AAC_NATIVE)
    list(APPEND SEURAT_INTERNAL_TARGETS seurat-aac-native)
endif()

# We depend on external projects to ensure their .a files exist before we link
set(SEURAT_EXT_DEPENDENCIES "")
set(SEURAT_EXT_ARCHIVES "")

if(SEURAT_CRYPTO)
    list(APPEND SEURAT_EXT_DEPENDENCIES openssl)
    list(APPEND SEURAT_EXT_ARCHIVES "${OUT_DIR}/lib/libssl.a" "${OUT_DIR}/lib/libcrypto.a")
endif()
if(SEURAT_SRT)
    list(APPEND SEURAT_EXT_DEPENDENCIES srt)
    list(APPEND SEURAT_EXT_ARCHIVES "${OUT_DIR}/lib/libsrt.a")
endif()
if(SEURAT_YUV)
    list(APPEND SEURAT_EXT_DEPENDENCIES libyuv)
    list(APPEND SEURAT_EXT_ARCHIVES "${OUT_DIR}/lib/libyuv.a")
endif()
if(SEURAT_OPENH264)
    list(APPEND SEURAT_EXT_DEPENDENCIES openh264)
    list(APPEND SEURAT_EXT_ARCHIVES "${OUT_DIR}/lib/libopenh264.a")
endif()

if(SEURAT_EXT_DEPENDENCIES)
    add_dependencies(seurat ${SEURAT_EXT_DEPENDENCIES})
endif()
if(SEURAT_INTERNAL_TARGETS)
    add_dependencies(seurat ${SEURAT_INTERNAL_TARGETS})
endif()

# Link everything together, forcing inclusion of all symbols so they are exported
if(APPLE)
    foreach(_target ${SEURAT_INTERNAL_TARGETS})
        target_link_libraries(seurat PRIVATE "-Wl,-force_load,$<TARGET_FILE:${_target}>")
    endforeach()
    foreach(_archive ${SEURAT_EXT_ARCHIVES})
        target_link_libraries(seurat PRIVATE "-Wl,-force_load,${_archive}")
    endforeach()
    
    # Frameworks required by native components
    target_link_libraries(seurat PRIVATE "-framework VideoToolbox" "-framework AudioToolbox" "-framework CoreFoundation" "-framework CoreVideo" "-framework CoreMedia")
elseif(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    target_link_libraries(seurat PRIVATE "-Wl,--whole-archive")
    foreach(_target ${SEURAT_INTERNAL_TARGETS})
        target_link_libraries(seurat PRIVATE ${_target})
    endforeach()
    foreach(_archive ${SEURAT_EXT_ARCHIVES})
        target_link_libraries(seurat PRIVATE "${_archive}")
    endforeach()
    target_link_libraries(seurat PRIVATE "-Wl,--no-whole-archive")
    
    # Android specific libraries
    if(ANDROID)
        target_link_libraries(seurat PRIVATE mediandk log android)
    endif()
endif()

install(TARGETS seurat DESTINATION "${OUT_DIR}/lib")
