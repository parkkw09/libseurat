# includes
include(ExternalProject)

set(PRE_BUILDER "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builder/prebuild")
set(CMAKE_MODULE_PATH "${PRE_BUILDER}" "${CMAKE_MODULE_PATH}")
set(BUILDER_NAME "x264")

list(APPEND X264_CONFIGURE_OPTIONS --enable-pic --enable-static --enable-strip --disable-cli --disable-win32thread --disable-avs --disable-swscale --disable-lavf --disable-ffms --disable-gpac --disable-lsmash)
list(APPEND X264_BUILD_OPTIONS all install)

if (ANDROID)
    set(X264_HOST ${CMAKE_CXX_ANDROID_TOOLCHAIN_MACHINE})
    set(X264_SYSROOT ${ANDROID_SYSROOT_ROOT})
    set(X264_CROSS_PREFIX ${CMAKE_CXX_ANDROID_TOOLCHAIN_PREFIX})
    set(X264_OUT_DIR ${OUT_DIR})
elseif (IOS)
endif()

message(STATUS "X264_HOST = ${X264_HOST}")
message(STATUS "X264_SYSROOT = ${X264_SYSROOT}")
message(STATUS "X264_CROSS_PREFIX = ${X264_CROSS_PREFIX}")
message(STATUS "X264_CONFIGURE_OPTIONS = ${X264_CONFIGURE_OPTIONS}")
message(STATUS "X264_BUILD_OPTIONS = ${X264_BUILD_OPTIONS}")
message(STATUS "X264_OUT_DIR = ${X264_OUT_DIR}")

# add X264 target
ExternalProject_Add(${BUILDER_NAME}
    PREFIX ${BUILDER_NAME}
    BUILD_IN_SOURCE 1
    GIT_REPOSITORY "https://code.videolan.org/videolan/x264.git"
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ./Configure --host=${X264_HOST} --sysroot=${X264_SYSROOT} --cross-prefix=${X264_CROSS_PREFIX} --prefix=${X264_OUT_DIR} ${X264_CONFIGURE_OPTIONS}
    BUILD_COMMAND make ${X264_BUILD_OPTIONS}
    INSTALL_COMMAND ""
)