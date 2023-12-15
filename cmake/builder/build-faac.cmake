# includes
include(ExternalProject)

set(FAAC_BUILDER "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builder/faac")
set(CMAKE_MODULE_PATH "${FAAC_BUILDER}" "${CMAKE_MODULE_PATH}")

set(FAAC_ARCH "")
set(PRE_BUILD "")
list(APPEND FAAC_CONFIGURE_OPTIONS "")
list(APPEND FAAC_BUILD_OPTIONS all install)

if (ANDROID)
    set(FAAC_ARCH "android")
    set(PRE_BUILD ${FAAC_BUILDER}/pre_build_android.sh -t ${ANDROID_TOOLCHAIN_ROOT} -h ${ANDROID_TARGET_HOST} -a ${ANDROID_TARGET_HOST_API})
elseif (IOS)
    if(LEONARDO_ARCH STREQUAL "x86")
        set(FAAC_ARCH "iossimulator-xcrun")
    elseif(LEONARDO_ARCH STREQUAL "x86_64")
        set(FAAC_ARCH "iossimulator-xcrun")
    elseif(LEONARDO_ARCH STREQUAL "armeabi-v7a")
        set(FAAC_ARCH "ios-cross")
    elseif(LEONARDO_ARCH STREQUAL "arm64-v8a")
        set(FAAC_ARCH "ios64-cross")
    else()
        message(FATAL_ERROR "Unknown LEONARDO_ARCH = [${LEONARDO_ARCH}]")
    endif()
    set(FAAC_BUILD_VERSION "1.1.1d")
    list(APPEND FAAC_CONFIGURE_OPTIONS "--prefix=${OUT_DIR}")
    # list(APPEND FAAC_BUILD_OPTIONS install_ssldirs)
    set(PRE_BUILD ${FAAC_BUILDER}/pre_build_ios.sh -t ${TOOLCHAIN_PATH} -p ${TOOLCHAIN_CROSS_TOP} -s ${TOOLCHAIN_CROSS_IPHONE_SDK})
endif()
 
# add FAAC target
ExternalProject_Add(FAAC-EXTERNAL
    GIT_REPOSITORY "https://github.com/knik0/faac.git"
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND cd <SOURCE_DIR> && source ${PRE_BUILD}
    CONFIGURE_COMMAND && ./bootstrap
    CONFIGURE_COMMAND && ./Configure --host=${ANDROID_TARGET_HOST} --with-sysroot=${ANDROID_TOOLCHAIN_ROOT} --prefix=${OUT_DIR} --disable-shared --enable-static --disable-faac --with-mp4v2
    BUILD_COMMAND cd <SOURCE_DIR> && source ${PRE_BUILD}
    BUILD_COMMAND && make ${FAAC_BUILD_OPTIONS}
    INSTALL_COMMAND ""
)