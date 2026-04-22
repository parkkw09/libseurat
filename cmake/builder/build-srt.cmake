# =============================================================================
# Haivision SRT ExternalProject builder.
#
# Links against the OpenSSL static libs we produced earlier in the same OUT_DIR
# (that's why we DEPENDS on the `openssl` ExternalProject when crypto is ON).
#
# Produces libsrt.a + headers under ${OUT_DIR}.
# =============================================================================

include(ExternalProject)

set(SRT_VERSION "1.5.3" CACHE STRING "SRT source tag")

set(_srt_args
    "-DCMAKE_INSTALL_PREFIX=${OUT_DIR}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    # SRT 1.5.x still declares cmake_minimum_required(VERSION 2.8). CMake 4+
    # refuses that; bump the effective policy baseline so it keeps working.
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
    "-DENABLE_APPS=OFF"
    "-DENABLE_SHARED=OFF"
    "-DENABLE_STATIC=ON"
    "-DENABLE_ENCRYPTION=${LEONARDO_CRYPTO}"
    "-DENABLE_HEAVY_LOGGING=OFF"
    "-DUSE_OPENSSL_PC=OFF")

if(LEONARDO_CRYPTO)
    list(APPEND _srt_args
        "-DUSE_ENCLIB=openssl"
        "-DOPENSSL_USE_STATIC_LIBS=TRUE"
        "-DOPENSSL_ROOT_DIR=${OUT_DIR}"
        "-DOPENSSL_INCLUDE_DIR=${OUT_DIR}/include"
        "-DOPENSSL_CRYPTO_LIBRARY=${OUT_DIR}/lib/libcrypto.a"
        "-DOPENSSL_SSL_LIBRARY=${OUT_DIR}/lib/libssl.a")
endif()

set(_srt_depends "")
if(LEONARDO_CRYPTO)
    list(APPEND _srt_depends openssl)
endif()

if(ANDROID)
    list(APPEND _srt_args
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake"
        "-DANDROID_ABI=${LEONARDO_ARCH}"
        "-DANDROID_PLATFORM=android-${LEONARDO_ANDROID_MIN_API}"
        "-DANDROID_STL=c++_static")

elseif(IOS)
    list(APPEND _srt_args
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain/toolchain-ios.cmake"
        "-DLEONARDO_TARGET:STRING=IOS"
        "-DLEONARDO_ARCH:STRING=${LEONARDO_ARCH}"
        "-DLEONARDO_IOS_DEPLOYMENT_TARGET:STRING=${LEONARDO_IOS_DEPLOYMENT_TARGET}")

elseif(APPLE)
    list(APPEND _srt_args
        "-DCMAKE_OSX_ARCHITECTURES=${LEONARDO_ARCH}"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=${LEONARDO_OSX_DEPLOYMENT_TARGET}")

elseif(MSVC)
    # nothing extra

else()
    message(FATAL_ERROR "SRT: target [${LEONARDO_TARGET}] not supported")
endif()

message(STATUS "SRT_VERSION = ${SRT_VERSION}")
message(STATUS "SRT_ARGS    = ${_srt_args}")
message(STATUS "SRT_DEPENDS = ${_srt_depends}")

ExternalProject_Add(srt
    PREFIX          srt
    DEPENDS         ${_srt_depends}
    GIT_REPOSITORY  "https://github.com/Haivision/srt.git"
    GIT_TAG         "v${SRT_VERSION}"
    GIT_SHALLOW     TRUE
    UPDATE_COMMAND  ""
    CMAKE_ARGS      ${_srt_args})
