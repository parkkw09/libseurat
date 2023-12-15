# includes
include(ExternalProject)

set(USE_OPENSSL_PC OFF)
set(ENABLE_SHARED ${LEONARDO_SHARED_ALL})

list(APPEND YUV_OPTIONS "-DENABLE_SHARED=${ENABLE_SHARED}")

# message(STATUS "[${TOOLCHAIN_OPTIONS} ${COMMON_OPTIONS} ${YUV_OPTIONS}]")

ExternalProject_Add(YUV
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/yuv
        CMAKE_ARGS ${TOOLCHAIN_OPTIONS} ${COMMON_OPTIONS} ${YUV_OPTIONS}
    )

# add YUV target
ExternalProject_Add(YUV
    GIT_REPOSITORY "https://code.videolan.org/videolan/x264.git"
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND cd <SOURCE_DIR>
    CONFIGURE_COMMAND && ./Configure --host=${ANDROID_TARGET_HOST} --sysroot=${ANDROID_TOOLCHAIN_ROOT} --prefix=${OUT_DIR} --enable-pic --enable-static --enable-strip --disable-cli --disable-win32thread --disable-avs --disable-swscale --disable-lavf --disable-ffms --disable-gpac --disable-lsmash
    BUILD_COMMAND cd <SOURCE_DIR> && source ${PRE_BUILD}
    BUILD_COMMAND && make ${X264_BUILD_OPTIONS}
    INSTALL_COMMAND ""
)