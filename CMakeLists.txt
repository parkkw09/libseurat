cmake_minimum_required(VERSION 3.4.1 FATAL_ERROR)

option(LEONARDO_CRYPTO "LEONARDO Support Crypto Algorithm." ON)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake" "${CMAKE_MODULE_PATH}")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/builder" "${CMAKE_MODULE_PATH}")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain" "${CMAKE_MODULE_PATH}")

if(LEONARDO_TARGET STREQUAL "ANDROID")
    include(toolchain-android)
elseif(LEONARDO_TARGET STREQUAL "IOS")
    include(toolchain-ios)
elseif(LEONARDO_TARGET STREQUAL "MSVC")
    include(toolchain-msvc)
elseif(LEONARDO_TARGET STREQUAL "OSX")
    include(toolchain-osx)
else()
    message(FATAL_ERROR "Unknown LEONARDO_ARCH = [${LEONARDO_ARCH}]")
endif()

project(LEONARDO VERSION 0.1.0)

include(toolchain-common)

message(STATUS "Compiler")
message(STATUS " - System   \t: ${CMAKE_SYSTEM_NAME}")
message(STATUS " - ID       \t: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS " - Version  \t: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS " - Path     \t: ${CMAKE_CXX_COMPILER}")
message(STATUS " - Path     \t: ${CMAKE_C_COMPILER}")
message(STATUS " - Prefix   \t: ${CMAKE_CXX_ANDROID_TOOLCHAIN_PREFIX}")
message(STATUS " - Machine  \t: ${CMAKE_CXX_ANDROID_TOOLCHAIN_MACHINE}")

if(LEONARDO_CRYPTO)
    include(build-openssl)
endif()
#include(build-x264)


#include(build-yuv)
#include(build-rtmp)
#include(build-srt)
#include(build-rtc)

# libseurat.so
# include(build-seurat)

#include(build-faac)
#include(build-open-h264)