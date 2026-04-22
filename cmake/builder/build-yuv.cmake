# =============================================================================
# libyuv ExternalProject builder.
#
# libyuv is a native-CMake project maintained by Chromium. Its stock
# CMakeLists.txt always emits a `yuv_shared` (libyuv.dylib/.so) target and an
# install step that links it. The shared variant is noisy across our targets:
#   * On Apple Silicon hosts doing an x86_64 OSX/iOS-sim slice, CMake reports
#     the host arch in CMAKE_SYSTEM_PROCESSOR and the shared-lib link pulls in
#     the wrong NEON objects.
#   * On iOS the simulator link step drags in Homebrew libjpeg (arm64-only).
#
# We only consume libyuv.a. Skip the shared target entirely and run a custom
# install that just copies the static archive plus headers.
# =============================================================================

include(ExternalProject)
include(ProcessorCount)

ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 1)
endif()

set(LIBYUV_GIT_REPO "https://chromium.googlesource.com/libyuv/libyuv"
    CACHE STRING "libyuv git source (Chromium upstream)")
set(LIBYUV_GIT_TAG  "main"
    CACHE STRING "libyuv git tag/branch")

set(_yuv_args
    "-DCMAKE_INSTALL_PREFIX=${OUT_DIR}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DBUILD_SHARED_LIBS=OFF"
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    # libyuv's MJPEG path is optional. Homebrew libjpeg is arm64-only on Apple
    # Silicon and breaks x86_64 slices; we don't need MJPEG so drop it entirely.
    "-DCMAKE_DISABLE_FIND_PACKAGE_JPEG=TRUE")

if(ANDROID)
    list(APPEND _yuv_args
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake"
        "-DANDROID_ABI=${LEONARDO_ARCH}"
        "-DANDROID_PLATFORM=android-${LEONARDO_ANDROID_MIN_API}"
        "-DANDROID_STL=c++_static")

elseif(IOS)
    # Forward our iOS toolchain so the same arch/SDK logic is reused.
    list(APPEND _yuv_args
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain/toolchain-ios.cmake"
        "-DLEONARDO_TARGET:STRING=IOS"
        "-DLEONARDO_ARCH:STRING=${LEONARDO_ARCH}"
        "-DLEONARDO_IOS_DEPLOYMENT_TARGET:STRING=${LEONARDO_IOS_DEPLOYMENT_TARGET}")

elseif(APPLE)
    # libyuv keys NEON / NEON64 / SVE source-lists off CMAKE_SYSTEM_PROCESSOR.
    # On Apple Silicon hosts CMake would otherwise fill that with "arm64" for
    # every sub-build, dragging NEON64 sources into x86_64 slices. Flip CMake
    # into cross-compile mode (CMAKE_SYSTEM_NAME=Darwin) so that our explicit
    # SYSTEM_PROCESSOR sticks.
    list(APPEND _yuv_args
        "-DCMAKE_OSX_ARCHITECTURES=${LEONARDO_ARCH}"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=${LEONARDO_OSX_DEPLOYMENT_TARGET}"
        "-DCMAKE_SYSTEM_NAME=Darwin"
        "-DCMAKE_SYSTEM_PROCESSOR=${LEONARDO_ARCH}")

elseif(MSVC)
    # MSVC: default generator suffices; nothing extra to do.

else()
    message(FATAL_ERROR "libyuv: target [${LEONARDO_TARGET}] not supported")
endif()

message(STATUS "LIBYUV_GIT_REPO = ${LIBYUV_GIT_REPO}")
message(STATUS "LIBYUV_GIT_TAG  = ${LIBYUV_GIT_TAG}")

ExternalProject_Add(libyuv
    PREFIX          libyuv
    GIT_REPOSITORY  "${LIBYUV_GIT_REPO}"
    GIT_TAG         "${LIBYUV_GIT_TAG}"
    GIT_SHALLOW     TRUE
    UPDATE_COMMAND  ""
    CMAKE_ARGS      ${_yuv_args}
    # Build only the static archive target (skip yuv_shared, cpuid, yuvconvert,
    # yuvconstants — we don't ship any of those).
    BUILD_COMMAND
        ${CMAKE_COMMAND} --build <BINARY_DIR> --target yuv --parallel ${NPROC}
    # Custom install: copy libyuv.a + headers; bypass the stock install(TARGETS)
    # rules that insist on yuv_shared existing.
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${OUT_DIR}/lib
    COMMAND
        ${CMAKE_COMMAND} -E make_directory ${OUT_DIR}/include
    COMMAND
        ${CMAKE_COMMAND} -E copy <BINARY_DIR>/libyuv.a ${OUT_DIR}/lib/libyuv.a
    COMMAND
        ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/include ${OUT_DIR}/include)
