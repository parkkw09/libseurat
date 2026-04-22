# =============================================================================
# MSVC toolchain (64-bit only: x86_64)
#
# On MSVC, CMake itself configures the compiler; this file mainly validates
# the request and emits consistent TOOLCHAIN/COMMON options.
# =============================================================================

message(STATUS "LEONARDO_ARCH = ${LEONARDO_ARCH}")

if(NOT LEONARDO_ARCH STREQUAL "x86_64")
    message(FATAL_ERROR
        "Unsupported LEONARDO_ARCH = [${LEONARDO_ARCH}]. "
        "MSVC 64-bit build supports: x86_64.")
endif()

set(CMAKE_SYSTEM_NAME "Windows")
set(CMAKE_SYSTEM_PROCESSOR "AMD64")

message(STATUS "CMAKE_SYSTEM_NAME      = [${CMAKE_SYSTEM_NAME}]")
message(STATUS "CMAKE_SYSTEM_PROCESSOR = [${CMAKE_SYSTEM_PROCESSOR}]")

list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
list(APPEND TOOLCHAIN_OPTIONS "-DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}")

set(OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/out/${LEONARDO_ARCH})

list(APPEND COMMON_OPTIONS "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
list(APPEND COMMON_OPTIONS "-DCMAKE_PREFIX_PATH=${OUT_DIR}")
list(APPEND COMMON_OPTIONS "-DCMAKE_INSTALL_PREFIX=${OUT_DIR}")

message(STATUS "OUT_DIR          = ${OUT_DIR}")
message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
