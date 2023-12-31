# Building for iOS is only available under APPLE systems
if(NOT APPLE)
    message(FATAL_ERROR "You need to build using a Mac OS X system")
endif()

message(STATUS "LEONARDO_ARCH = ${LEONARDO_ARCH}")

if(LEONARDO_ARCH STREQUAL "x86")
    set(CMAKE_SYSTEM_NAME "iOS")
    set(CMAKE_OSX_ARCHITECTURES "i386")
    set(PLATFORM "Simulator")
elseif(LEONARDO_ARCH STREQUAL "x86_64")
    set(CMAKE_SYSTEM_NAME "iOS")
    set(CMAKE_OSX_ARCHITECTURES "x86_64")
    set(PLATFORM "Simulator")
elseif(LEONARDO_ARCH STREQUAL "armv7")
    set(CMAKE_SYSTEM_NAME "iOS")
    set(CMAKE_OSX_ARCHITECTURES "armv7")
    set(PLATFORM "OS")
elseif(LEONARDO_ARCH STREQUAL "arm64")
    set(CMAKE_SYSTEM_NAME "iOS")
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    set(PLATFORM "OS")
else()
    message(FATAL_ERROR "Unknown LEONARDO_ARCH = [${LEONARDO_ARCH}]")
endif()

set(CMAKE_MACOSX_RPATH 1)

execute_process(COMMAND xcode-select -print-path
    RESULT_VARIABLE XCODE_SELECT_RESULT
    OUTPUT_VARIABLE XCODE_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT ${XCODE_SELECT_RESULT} EQUAL 0)
	message(FATAL_ERROR "xcode-select failed: ${XCODE_SELECT_RESULT}. You may need to install Xcode.")
endif()

string(TOLOWER ${PLATFORM}  PLATFORM_LOWER)

execute_process(COMMAND xcrun --sdk iphone${PLATFORM_LOWER} --show-sdk-version
    RESULT_VARIABLE XCRUN_SHOW_SDK_VERSION_RESULT
    OUTPUT_VARIABLE IOS_SDK_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT ${XCRUN_SHOW_SDK_VERSION_RESULT} EQUAL 0)
    message(FATAL_ERROR "xcrun failed: ${XCRUN_SHOW_SDK_VERSION_RESULT}. You may need to install Xcode.")
endif()

execute_process(COMMAND xcrun --sdk iphone${PLATFORM_LOWER} --show-sdk-path
    RESULT_VARIABLE XCRUN_SHOW_SDK_PATH_RESULT
    OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT ${XCRUN_SHOW_SDK_PATH_RESULT} EQUAL 0)
    message(FATAL_ERROR "xcrun failed: ${XCRUN_SHOW_SDK_PATH_RESULT}. You may need to install Xcode.")
endif()

execute_process(COMMAND xcrun --sdk iphone${PLATFORM_LOWER} --show-sdk-platform-path
    RESULT_VARIABLE XCRUN_SHOW_SDK_PATH_RESULT
    OUTPUT_VARIABLE CMAKE_OSX_PLATFORM_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT ${XCRUN_SHOW_SDK_PATH_RESULT} EQUAL 0)
    message(FATAL_ERROR "xcrun failed: ${XCRUN_SHOW_SDK_PATH_RESULT}. You may need to install Xcode.")
endif()

execute_process(COMMAND xcrun --sdk iphone${PLATFORM_LOWER} --find clang
    RESULT_VARIABLE XCRUN_FIND_CLANG_RESULT
    OUTPUT_VARIABLE CLANG_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT ${XCRUN_FIND_CLANG_RESULT} EQUAL 0)
    message(FATAL_ERROR "xcrun failed: ${XCRUN_FIND_CLANG_RESULT}. You may need to install Xcode.")
endif()
get_filename_component(TOOLCHAIN_PATH "${CLANG_PATH}" DIRECTORY)

message(STATUS "Using sysroot path: ${CMAKE_OSX_SYSROOT}")
message(STATUS "Using platform path: ${CMAKE_OSX_PLATFORM_PATH}")
message(STATUS "Using sdk version: ${IOS_SDK_VERSION}")
message(STATUS "Using sdk full version: iPhone${PLATFORM}${IOS_SDK_VERSION}.sdk")
set(SDK_BIN_PATH "${CMAKE_OSX_SYSROOT}/../../usr/bin")

set(TOOLCHAIN_CC "${TOOLCHAIN_PATH}/clang")
set(TOOLCHAIN_CXX "${TOOLCHAIN_PATH}/clang++")
set(TOOLCHAIN_OBJC "${TOOLCHAIN_PATH}/clang")
set(TOOLCHAIN_LD "${TOOLCHAIN_PATH}/ld")
set(TOOLCHAIN_AR "${TOOLCHAIN_PATH}/ar")
set(TOOLCHAIN_RANLIB "${TOOLCHAIN_PATH}/ranlib")
set(TOOLCHAIN_STRIP "${TOOLCHAIN_PATH}/strip")
set(TOOLCHAIN_NM "${TOOLCHAIN_PATH}/nm")

set(TOOLCHAIN_CROSS_TOP ${CMAKE_OSX_PLATFORM_PATH}/Developer)
set(TOOLCHAIN_CROSS_IPHONE_SDK iPhone${PLATFORM}${IOS_SDK_VERSION}.sdk)

execute_process(COMMAND xcodebuild -version OUTPUT_VARIABLE XCODE_VERSION_RAW OUTPUT_STRIP_TRAILING_WHITESPACE)
STRING(REGEX REPLACE "Xcode ([^\n]*).*" "\\1" XCODE_VERSION "${XCODE_VERSION_RAW}")

include(CMakeForceCompiler)

set(CMAKE_CROSSCOMPILING TRUE)

# Define name of the target system
set(CMAKE_SYSTEM_VERSION ${IOS_SDK_VERSION})
set(IOS True)

# The following variables are needed to build correctly with Xcode
if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(CMAKE_MACOSX_BUNDLE YES)
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO)
    set(CMAKE_XCODE_ATTRIBUTE_BITCODE_GENERATION_MODE "bitcode")
endif()

# Define the compiler
set(CMAKE_C_COMPILER "${TOOLCHAIN_CC}")
set(CMAKE_C_COMPILER_TARGET "${CLANG_TARGET}")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_CXX}")
set(CMAKE_CXX_COMPILER_TARGET "${CLANG_TARGET}")
set(CMAKE_AR "${TOOLCHAIN_AR}" CACHE FILEPATH "ar")
set(CMAKE_RANLIB "${TOOLCHAIN_RANLIB}" CACHE FILEPATH "ranlib")
set(CMAKE_LINKER "${TOOLCHAIN_LD}" CACHE FILEPATH "linker")
set(CMAKE_NM "${TOOLCHAIN_NM}" CACHE FILEPATH "nm")

set(CMAKE_FIND_ROOT_PATH ${CMAKE_OSX_SYSROOT} ${CMAKE_INSTALL_PREFIX})
# search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

message(STATUS "CMAKE_HOST_SYSTEM_NAME = [${CMAKE_HOST_SYSTEM_NAME}]")
message(STATUS "CMAKE_SYSTEM_NAME = [${CMAKE_SYSTEM_NAME}]")
message(STATUS "CMAKE_SYSTEM_VERSION = [${CMAKE_SYSTEM_VERSION}]")
message(STATUS "CMAKE_OSX_ARCHITECTURES = [${CMAKE_OSX_ARCHITECTURES}]")
message(STATUS "CMAKE_OSX_SYSROOT = [${CMAKE_OSX_SYSROOT}]")

list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_OSX_DEPLOYMENT_TARGET=7.0")

set(OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/out/${CMAKE_OSX_ARCHITECTURES})

list(APPEND COMMON_OPTIONS "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
list(APPEND COMMON_OPTIONS "-DCMAKE_PREFIX_PATH=${OUT_DIR}")
list(APPEND COMMON_OPTIONS "-DCMAKE_INSTALL_PREFIX=${OUT_DIR}")
list(APPEND COMMON_OPTIONS "-DCMAKE_FIND_ROOT_PATH=${OUT_DIR}")

message(STATUS "OUT_DIR = ${OUT_DIR}")
message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")
message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")