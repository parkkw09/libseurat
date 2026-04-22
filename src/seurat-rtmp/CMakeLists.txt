# =============================================================================
# seurat-rtmp — RTMP(S) publisher client (internal, MIT).
#
# Links against seurat-flv (for payload framing) and — when LEONARDO_CRYPTO is
# ON — against the OpenSSL static libs already built into OUT_DIR/lib by the
# build-openssl.cmake ExternalProject (for RTMPS / TLS).
#
# Produces libseurat-rtmp.a + public header under ${OUT_DIR}.
#
# Build phases (historical):
#   Phase A   -> skeleton: public API + lifecycle stubs.
#   Phase B   -> handshake, AMF0, chunk stream, publish negotiation.
#   Phase C   -> AV payload path (metadata, AVC/AAC sequence + frames).
#   Phase D   -> RTMPS (OpenSSL BIO-wrapped TlsTransport). Active when
#                 LEONARDO_CRYPTO=ON; the TLS source is unconditionally
#                 compiled but its body is guarded by SEURAT_RTMP_WITH_TLS,
#                 so plain-RTMP builds produce an empty translation unit.
# =============================================================================

set(_rtmp_sources
    src/rtmp.cpp
    src/rtmp_amf0.cpp
    src/rtmp_chunk.cpp
    src/rtmp_handshake.cpp
    src/rtmp_socket.cpp
    src/rtmp_tls.cpp)

add_library(seurat-rtmp STATIC ${_rtmp_sources})

target_include_directories(seurat-rtmp
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# seurat-flv's public headers live in its own include/.
target_include_directories(seurat-rtmp PRIVATE
    ${CMAKE_SOURCE_DIR}/src/seurat-flv/include)

set_target_properties(seurat-rtmp PROPERTIES
    CXX_STANDARD           17
    CXX_STANDARD_REQUIRED  ON
    CXX_EXTENSIONS         OFF
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME            seurat-rtmp)

target_compile_options(seurat-rtmp PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter)

# CMake-level ordering: seurat-flv must be built first (we include its header
# and may later call its API via static inline in rtmp.cpp).
add_dependencies(seurat-rtmp seurat-flv)

if(LEONARDO_CRYPTO)
    # OpenSSL static archives live under OUT_DIR/lib/ (installed by the
    # build-openssl.cmake ExternalProject). For the seurat-rtmp static
    # archive itself we only need the OpenSSL headers at compile time;
    # linking libssl/libcrypto is the downstream consumer's job (they are
    # the ones producing the final shared lib / executable).
    #
    # CMake-level ordering: seurat-rtmp cannot compile until OpenSSL's
    # install_sw step has materialised OUT_DIR/include/openssl/*.h, so we
    # pin a hard dependency on the ExternalProject target (BUILDER_NAME
    # is "openssl" in build-openssl.cmake).
    target_compile_definitions(seurat-rtmp PRIVATE SEURAT_RTMP_WITH_TLS=1)
    target_include_directories(seurat-rtmp PRIVATE ${OUT_DIR}/include)
    add_dependencies(seurat-rtmp openssl)
endif()

add_custom_command(TARGET seurat-rtmp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}/lib"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}/include/seurat"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:seurat-rtmp>"
            "${OUT_DIR}/lib/libseurat-rtmp.a"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/include/seurat/rtmp.h"
            "${OUT_DIR}/include/seurat/rtmp.h"
    COMMENT "[seurat-rtmp] install -> ${OUT_DIR}")
