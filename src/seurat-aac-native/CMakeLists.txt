cmake_minimum_required(VERSION 3.21)

project(seurat-aac-native C CXX)

set(SOURCES src/aac_native_common.c)

if(SEURAT_TARGET STREQUAL "IOS" OR SEURAT_TARGET STREQUAL "OSX")
    list(APPEND SOURCES src/aac_audiotoolbox.c)
elseif(SEURAT_TARGET STREQUAL "ANDROID")
    list(APPEND SOURCES src/aac_ndkmedia.c)
elseif(SEURAT_TARGET STREQUAL "MSVC")
    list(APPEND SOURCES src/aac_mft.cpp)
endif()

add_library(seurat-aac-native STATIC ${SOURCES})
target_include_directories(seurat-aac-native PUBLIC include)

if(SEURAT_TARGET STREQUAL "IOS" OR SEURAT_TARGET STREQUAL "OSX")
    target_link_libraries(seurat-aac-native "-framework AudioToolbox" "-framework CoreFoundation")
elseif(SEURAT_TARGET STREQUAL "ANDROID")
    target_link_libraries(seurat-aac-native mediandk)
endif()

install(TARGETS seurat-aac-native DESTINATION "${OUT_DIR}/lib")
install(DIRECTORY include/seurat DESTINATION "${OUT_DIR}/include")
