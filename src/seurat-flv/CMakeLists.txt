# =============================================================================
# seurat-flv — FLV muxer (internal, MIT).
#
# Produces libseurat-flv.a + headers. Drops them into the shared OUT_DIR so the
# per-platform packaging scripts (scripts/package-*.sh) pick them up alongside
# the external deps (OpenSSL, OpenH264, libyuv, ...).
# =============================================================================

add_library(seurat-flv STATIC
    src/flv.cpp)

target_include_directories(seurat-flv
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(seurat-flv PROPERTIES
    CXX_STANDARD           17
    CXX_STANDARD_REQUIRED  ON
    CXX_EXTENSIONS         OFF
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME            seurat-flv)

target_compile_options(seurat-flv PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Copy artefact + public header into OUT_DIR so the packaging scripts see it.
add_custom_command(TARGET seurat-flv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}/lib"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}/include/seurat"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:seurat-flv>"
            "${OUT_DIR}/lib/libseurat-flv.a"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/include/seurat/flv.h"
            "${OUT_DIR}/include/seurat/flv.h"
    COMMENT "[seurat-flv] install -> ${OUT_DIR}")
