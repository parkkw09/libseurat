cmake_minimum_required(VERSION 3.21)

project(seurat-h264-native C CXX)

set(SOURCES src/h264_native_common.c)

if(SEURAT_TARGET STREQUAL "IOS" OR SEURAT_TARGET STREQUAL "OSX")
    list(APPEND SOURCES src/h264_videotoolbox.c)
elseif(SEURAT_TARGET STREQUAL "ANDROID")
    list(APPEND SOURCES src/h264_ndkmedia.c)
elseif(SEURAT_TARGET STREQUAL "MSVC")
    list(APPEND SOURCES src/h264_mft.cpp)
endif()

add_library(seurat-h264-native STATIC ${SOURCES})
target_include_directories(seurat-h264-native PUBLIC include)

if(SEURAT_TARGET STREQUAL "IOS" OR SEURAT_TARGET STREQUAL "OSX")
    target_link_libraries(seurat-h264-native "-framework VideoToolbox" "-framework CoreVideo" "-framework CoreFoundation" "-framework CoreMedia")
elseif(SEURAT_TARGET STREQUAL "ANDROID")
    target_link_libraries(seurat-h264-native mediandk)
endif()

install(TARGETS seurat-h264-native DESTINATION "${OUT_DIR}/lib")
install(DIRECTORY include/seurat DESTINATION "${OUT_DIR}/include")
