#!/bin/bash

TMP=WORK
OUT=out
DIST=dist
TARGET=""
BUILD_TYPE="Release"
BUILD_OPTION=""
ARCHS="x86 x86_64 armeabi-v7a arm64-v8a"

if [ "$1" = "ANDROID" ] ; then
    echo "ANDROID build mode"
    TARGET="ANDROID"
    ARCHS="x86 x86_64 armeabi-v7a arm64-v8a"
elif [ "$1" = "IOS" ] ; then
    echo "IOS build mode"
    TARGET="IOS"
    ARCHS="x86 x86_64 armv7 arm64"
elif [ "$1" = "MSVC" ] ; then
    echo "MSVC build mode"
    TARGET="MSVC"
    ARCHS="x86 x86_64"
elif [ "$1" = "OSX" ] ; then
    echo "OSX build mode"
    TARGET="OSX"
    ARCHS="x86 x86_64"
else
    echo "unknown build mode"
    exit 0
fi

if [ "$2" = "DEBUG" ] ; then
    echo "build type DEBUG"
    BUILD_TYPE="Debug"
    BUILD_OPTION="VERBOSE=1"
elif [ "$2" = "DEV" ] ; then
    echo "build type DEV"
    BUILD_TYPE="Release"
    BUILD_OPTION="VERBOSE=1"
    ARCHS="x86_64"
else
    echo "build type RELEASE"
    BUILD_TYPE="Release"
    BUILD_OPTION=""
fi

if [ ! -d ${TMP} ]; then
 mkdir ${TMP}
fi

if [ ! -d ${DIST} ]; then
 mkdir ${DIST}
fi

if [ "$1" = "ANDROID" ] ; then

    if [ ! -d ${DIST}/jni ]; then
        mkdir ${DIST}/jni
    fi

    if [ ! -d ${DIST}/jniLibs ]; then
        mkdir ${DIST}/jniLibs
    fi
fi

for ARCH in ${ARCHS}; do
#for ARCH in x86_64; do

    echo ${TMP}/${ARCH}

    if [ ! -d ${TMP}/${ARCH} ]; then
        mkdir ${TMP}/${ARCH}
    fi

    cmake . -B ${TMP}/${ARCH} -D LEONARDO_ARCH=${ARCH} -D LEONARDO_TARGET=$TARGET -D LEONARDO_CRYPTO=ON -D LEONARDO_SHARED_ALL=OFF -D CMAKE_BUILD_TYPE=$BUILD_TYPE
    cd ${TMP}/${ARCH}

    make $BUILD_OPTION
    make install
    cd ../..

    if [ "$1" = "ANDROID" ] ; then

        if [ ! -d ${DIST}/jniLibs/${ARCH} ]; then
            mkdir ${DIST}/jniLibs/${ARCH}
        fi

        TEMP_OUT_DIR=${OUT}/${ARCH}
        TEMP_DIST_DIR=${DIST}/jniLibs/${ARCH}

#        cp -r ${TEMP_OUT_DIR}/include ${DIST}/jni
#        cp ${TEMP_OUT_DIR}/lib/libseurat.* ${TEMP_DIST_DIR}/
#        cp ${TEMP_OUT_DIR}/lib/libjingle_peerconnection*.* ${TEMP_DIST_DIR}/
    fi
done
