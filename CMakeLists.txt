cmake_minimum_required(VERSION 3.15)
project(NessMuxer C)
set(CMAKE_C_STANDARD 11)

# ===========================================================================
# Options — o usuario escolhe quais backends extras quer compilar.
# O backend nativo da plataforma (MF no Windows, VT no macOS) e automatico.
# ===========================================================================

option(NESS_USE_X264  "Enable libx264 software encoder backend"  OFF)
option(NESS_USE_NVENC "Enable NVIDIA NVENC hardware encoder backend" OFF)
option(NESS_USE_N148 "Enable N.148 autoral codec" OFF)

# Paths so necessarios no Windows (Linux/macOS usam pkg-config / frameworks)
set(NESS_X264_INCLUDE_DIR  "" CACHE PATH "Path to x264 headers (Windows only)")
set(NESS_NVENC_INCLUDE_DIR "" CACHE PATH "Path to NVIDIA Video Codec SDK headers")

# ===========================================================================
# NessMuxer — shared library
# ===========================================================================

add_library(NessMuxer SHARED
    src/ness_muxer.c
    src/encoder/encoder.c
    src/encoder/nvenc_encoder.c
    src/mkv_muxer.c
    src/ebml_writer.c
    src/avc_utils.c
    src/buffered_io.c
)

target_include_directories(NessMuxer PUBLIC include)
target_include_directories(NessMuxer PRIVATE src)
target_compile_definitions(NessMuxer PRIVATE NESSMUXER_EXPORTS)

# ---------------------------------------------------------------------------
# Platform-native encoder: Media Foundation (Windows) / VideoToolbox (macOS)
# ---------------------------------------------------------------------------

if(WIN32)
    target_sources(NessMuxer PRIVATE
        src/encoder/mf_encoder_backend.c
        src/mf_encoder.c
    )
    target_link_libraries(NessMuxer PRIVATE
        mfplat mfuuid mf ole32 wmcodecdspuuid
    )
elseif(APPLE)
    target_sources(NessMuxer PRIVATE
        src/encoder/vtbox_encoder.c
    )
    target_link_libraries(NessMuxer PRIVATE
        "-framework VideoToolbox"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreFoundation"
    )
    target_compile_definitions(NessMuxer PRIVATE NESS_HAVE_VIDEOTOOLBOX)
endif()

# ---------------------------------------------------------------------------
# Optional: libx264 (cross-platform software encoder)
# Windows  -> LoadLibrary dinamico, so precisa dos headers (NESS_X264_INCLUDE_DIR)
# Linux/macOS -> linka via pkg-config
# ---------------------------------------------------------------------------

if(NESS_USE_X264)
    target_sources(NessMuxer PRIVATE
        src/encoder/x264_encoder.c
    )
    target_compile_definitions(NessMuxer PRIVATE NESS_HAVE_X264)

    if(WIN32)
        if(NOT NESS_X264_INCLUDE_DIR)
            message(FATAL_ERROR
                "NESS_USE_X264=ON on Windows requires NESS_X264_INCLUDE_DIR "
                "(path to x264.h / x264_config.h)")
        endif()
        target_include_directories(NessMuxer PRIVATE ${NESS_X264_INCLUDE_DIR})
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(X264 REQUIRED x264)
        target_include_directories(NessMuxer PRIVATE ${X264_INCLUDE_DIRS})
        target_link_libraries(NessMuxer PRIVATE ${X264_LIBRARIES})
    endif()
endif()

# ---------------------------------------------------------------------------
# Optional: NVIDIA NVENC (Windows e Linux)
# ---------------------------------------------------------------------------

if(NESS_USE_NVENC)
    if(NOT NESS_NVENC_INCLUDE_DIR)
        message(FATAL_ERROR
            "NESS_USE_NVENC=ON requires NESS_NVENC_INCLUDE_DIR "
            "(path to nvEncodeAPI.h)")
    endif()
    target_include_directories(NessMuxer PRIVATE ${NESS_NVENC_INCLUDE_DIR})
    target_compile_definitions(NessMuxer PRIVATE NESS_HAVE_NVENC)
endif()

# ===========================================================================
# Tests
# ===========================================================================

# --- test_ebml (pure C, cross-platform) ---
add_executable(test_ebml
    test/test_ebml.c
    src/buffered_io.c
    src/ebml_writer.c
)
target_include_directories(test_ebml PRIVATE src)

# --- test_avc (pure C, cross-platform) ---
add_executable(test_avc
    test/test_avc.c
    src/avc_utils.c
)
target_include_directories(test_avc PRIVATE src)

# --- test_encoder (Windows only — usa MF diretamente) ---
if(WIN32)
    add_executable(test_encoder
        test/test_encoder.c
        src/mf_encoder.c
        src/avc_utils.c
    )
    target_include_directories(test_encoder PRIVATE src)
    target_link_libraries(test_encoder PRIVATE
        mfplat mfuuid mf ole32 wmcodecdspuuid
    )
endif()

# --- test_muxer_only (pure C, cross-platform) ---
add_executable(test_muxer_only
    test/test_muxer_only.c
    src/mkv_muxer.c
    src/ebml_writer.c
    src/buffered_io.c
    src/avc_utils.c
)
target_include_directories(test_muxer_only PRIVATE src)

# --- test_full_pipeline (usa a lib completa) ---
add_executable(test_full_pipeline
    test/test_full_pipeline.c
    src/ness_muxer.c
    src/encoder/encoder.c
    src/encoder/nvenc_encoder.c
    src/mkv_muxer.c
    src/ebml_writer.c
    src/avc_utils.c
    src/buffered_io.c
)
target_include_directories(test_full_pipeline PRIVATE src include)
target_compile_definitions(test_full_pipeline PRIVATE NESSMUXER_EXPORTS)

if(WIN32)
    target_sources(test_full_pipeline PRIVATE
        src/encoder/mf_encoder_backend.c
        src/mf_encoder.c
    )
    target_link_libraries(test_full_pipeline PRIVATE
        mfplat mfuuid mf ole32 wmcodecdspuuid
    )
elseif(APPLE)
    target_sources(test_full_pipeline PRIVATE
        src/encoder/vtbox_encoder.c
    )
    target_link_libraries(test_full_pipeline PRIVATE
        "-framework VideoToolbox"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreFoundation"
    )
    target_compile_definitions(test_full_pipeline PRIVATE NESS_HAVE_VIDEOTOOLBOX)
endif()

if(NESS_USE_X264)
    target_sources(test_full_pipeline PRIVATE
        src/encoder/x264_encoder.c
    )
    target_compile_definitions(test_full_pipeline PRIVATE NESS_HAVE_X264)

    if(WIN32)
        target_include_directories(test_full_pipeline PRIVATE
            ${NESS_X264_INCLUDE_DIR}
        )
    else()
        target_include_directories(test_full_pipeline PRIVATE ${X264_INCLUDE_DIRS})
        target_link_libraries(test_full_pipeline PRIVATE ${X264_LIBRARIES})
    endif()
endif()

if(NESS_USE_NVENC)
    target_include_directories(test_full_pipeline PRIVATE
        ${NESS_NVENC_INCLUDE_DIR}
    )
    target_compile_definitions(test_full_pipeline PRIVATE NESS_HAVE_NVENC)
endif()

# ---------------------------------------------------------------------------
# Optional: N.148 codec
# ---------------------------------------------------------------------------

if(NESS_USE_N148)
    target_compile_definitions(NessMuxer PRIVATE NESS_HAVE_N148)
    target_compile_definitions(test_full_pipeline PRIVATE NESS_HAVE_N148)

    target_sources(NessMuxer PRIVATE
        src/codec/n148/n148_bitstream.c
        src/codec/n148/n148_codec_private.c
        src/codec/n148/n148_codec.c
        src/decoder/n148/n148_decoder.c
        src/decoder/n148/n148_parser.c
        src/decoder/n148/n148_frame_recon.c
        src/decoder/n148/n148_motion.c
        src/decoder/n148/n148_dpb.c
    )
endif()

add_executable(test_n148_codec
    test/test_n148_codec.c
    src/codec/n148/n148_bitstream.c
    src/codec/n148/n148_codec_private.c
    src/decoder/n148/n148_decoder.c
    src/decoder/n148/n148_parser.c
    src/decoder/n148/n148_frame_recon.c
    src/decoder/n148/n148_motion.c
    src/decoder/n148/n148_dpb.c
)
target_include_directories(test_n148_codec PRIVATE src)

add_executable(test_n148_mux
    test/test_n148_mux.c
    src/mkv_muxer.c
    src/ebml_writer.c
    src/buffered_io.c
    src/avc_utils.c
    src/codec/n148/n148_codec.c
    src/codec/n148/n148_bitstream.c
    src/codec/n148/n148_codec_private.c
    src/decoder/n148/n148_parser.c
)
target_include_directories(test_n148_mux PRIVATE src)

# ===========================================================================
# CLI Tools
# ===========================================================================

add_executable(nessmux
    tools/nessmux.c
)
target_include_directories(nessmux PRIVATE include)
target_link_libraries(nessmux PRIVATE NessMuxer)

add_executable(nessmux_validate
    tools/nessmux_validate.c
    src/buffered_io.c
    src/codec/n148/n148_codec_private.c
)
target_include_directories(nessmux_validate PRIVATE src)

add_executable(nessmux_bench
    tools/nessmux_bench.c
)
target_include_directories(nessmux_bench PRIVATE include)
target_link_libraries(nessmux_bench PRIVATE NessMuxer)