cmake_minimum_required(VERSION 3.15)
project(NessMuxer C)
set(CMAKE_C_STANDARD 11)

# ===========================================================================
# Options — o usuario escolhe quais backends extras quer compilar.
# O backend nativo da plataforma (MF no Windows, VT no macOS) e automatico.
# ===========================================================================

option(NESS_USE_X264  "Enable libx264 software encoder backend" OFF)
option(NESS_USE_NVENC "Enable NVIDIA NVENC hardware encoder backend" OFF)
option(NESS_USE_N148  "Enable N.148 autoral codec" OFF)

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
else()
    # Linux needs libm for math functions
    target_link_libraries(NessMuxer PRIVATE m)
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
# Optional: N.148 codec
# ETAPA A = reorganizacao fisica do CABAC em modulos dedicados
# ===========================================================================

if(NESS_USE_N148)
    target_compile_definitions(NessMuxer PRIVATE NESS_HAVE_N148)

    target_sources(NessMuxer PRIVATE
        src/codec/n148/n148_bitstream.c
        src/codec/n148/n148_codec_private.c
        src/codec/n148/n148_codec.c

        src/decoder/n148/n148_decoder.c
        src/decoder/n148/n148_parser.c
        src/decoder/n148/n148_frame_recon.c
        src/decoder/n148/n148_motion.c
        src/decoder/n148/n148_dpb.c

        src/common/motion_search.c
        src/common/interpolation.c
        src/common/motion_search_qpel.c
        src/common/n148_metrics.c
        src/common/n148_tables.c

        src/common/entropy/n148_binarization.c
        src/common/entropy/n148_cavlc.c

        # FASE 7R / ETAPA A — CABAC modularizado
        src/common/entropy/n148_cabac.c
        src/common/entropy/n148_cabac_engine.c
        src/common/entropy/n148_cabac_contexts.c
        src/common/entropy/n148_cabac_binarization.c
        src/common/entropy/n148_cabac_syntax.c
        src/common/entropy/n148_cabac_tables.c

        src/encoder/n148/n148_encoder.c
        src/encoder/n148/n148_intra.c
        src/encoder/n148/n148_inter.c
        src/encoder/n148/n148_transform.c
        src/encoder/n148/n148_quant.c
        src/encoder/n148/n148_entropy_enc_cavlc.c
        src/encoder/n148/n148_ratecontrol_basic.c
        src/encoder/n148/n148_ratecontrol.c
        src/encoder/n148/n148_profiles.c
        src/encoder/n148/n148_reorder.c
    )

    add_subdirectory(lib/n148dec)
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

if(UNIX AND NOT APPLE)
    target_link_libraries(test_full_pipeline PRIVATE m)
endif()

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

if(NESS_USE_N148)
    target_compile_definitions(test_full_pipeline PRIVATE NESS_HAVE_N148)

    target_sources(test_full_pipeline PRIVATE
        src/codec/n148/n148_bitstream.c
        src/codec/n148/n148_codec_private.c
        src/codec/n148/n148_codec.c

        src/decoder/n148/n148_parser.c
        src/decoder/n148/n148_frame_recon.c

        src/common/motion_search.c
        src/common/interpolation.c
        src/common/motion_search_qpel.c
        src/common/n148_metrics.c
        src/common/n148_tables.c

        src/common/entropy/n148_binarization.c
        src/common/entropy/n148_cavlc.c

        # FASE 7R / ETAPA A — CABAC modularizado
        src/common/entropy/n148_cabac.c
        src/common/entropy/n148_cabac_engine.c
        src/common/entropy/n148_cabac_contexts.c
        src/common/entropy/n148_cabac_binarization.c
        src/common/entropy/n148_cabac_syntax.c
        src/common/entropy/n148_cabac_tables.c

        src/encoder/n148/n148_encoder.c
        src/encoder/n148/n148_intra.c
        src/encoder/n148/n148_inter.c
        src/encoder/n148/n148_transform.c
        src/encoder/n148/n148_quant.c
        src/encoder/n148/n148_entropy_enc_cavlc.c
        src/encoder/n148/n148_ratecontrol_basic.c
        src/encoder/n148/n148_ratecontrol.c
        src/encoder/n148/n148_profiles.c
        src/encoder/n148/n148_reorder.c
    )
endif()

# ===========================================================================
# N.148 Tests
# ===========================================================================

# --- Macro helper: lista completa de sources do N.148 encoder+decoder ---
# Usada por todos os testes que precisam do encoder+decoder completo
set(N148_FULL_SOURCES
    src/codec/n148/n148_bitstream.c
    src/codec/n148/n148_codec_private.c
    src/codec/n148/n148_codec.c
    src/decoder/n148/n148_decoder.c
    src/decoder/n148/n148_parser.c
    src/decoder/n148/n148_frame_recon.c
    src/decoder/n148/n148_motion.c
    src/decoder/n148/n148_dpb.c
    src/common/motion_search.c
    src/common/interpolation.c
    src/common/motion_search_qpel.c
    src/common/n148_tables.c
    src/common/entropy/n148_binarization.c
    src/common/entropy/n148_cavlc.c
    src/common/entropy/n148_cabac.c
    src/common/entropy/n148_cabac_engine.c
    src/common/entropy/n148_cabac_contexts.c
    src/common/entropy/n148_cabac_binarization.c
    src/common/entropy/n148_cabac_syntax.c
    src/common/entropy/n148_cabac_tables.c
    src/encoder/n148/n148_encoder.c
    src/encoder/n148/n148_intra.c
    src/encoder/n148/n148_inter.c
    src/encoder/n148/n148_transform.c
    src/encoder/n148/n148_quant.c
    src/encoder/n148/n148_entropy_enc_cavlc.c
    src/encoder/n148/n148_ratecontrol_basic.c
    src/encoder/n148/n148_ratecontrol.c
    src/encoder/n148/n148_profiles.c
    src/encoder/n148/n148_reorder.c
)

add_library(n148_full_obj OBJECT
    ${N148_FULL_SOURCES}
)
target_include_directories(n148_full_obj PUBLIC src)

add_executable(test_n148_codec
    test/test_n148_codec.c
    src/codec/n148/n148_bitstream.c
    src/codec/n148/n148_codec_private.c
    src/codec/n148/n148_codec.c
    src/decoder/n148/n148_decoder.c
    src/decoder/n148/n148_parser.c
    src/decoder/n148/n148_frame_recon.c
    src/decoder/n148/n148_motion.c
    src/decoder/n148/n148_dpb.c
    src/common/interpolation.c
    src/common/n148_tables.c
    src/common/entropy/n148_binarization.c
    src/common/entropy/n148_cavlc.c
    src/common/entropy/n148_cabac.c
    src/common/entropy/n148_cabac_engine.c
    src/common/entropy/n148_cabac_contexts.c
    src/common/entropy/n148_cabac_binarization.c
    src/common/entropy/n148_cabac_syntax.c
    src/common/entropy/n148_cabac_tables.c
    src/encoder/n148/n148_quant.c
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

add_executable(test_n148_encoder
    test/test_n148_encoder.c
    $<TARGET_OBJECTS:n148_full_obj>
)
target_include_directories(test_n148_encoder PRIVATE src)

add_executable(test_n148_phase7_epic
    test/test_n148_phase7_epic.c
    src/mkv_muxer.c
    src/ebml_writer.c
    src/buffered_io.c
    src/avc_utils.c
    $<TARGET_OBJECTS:n148_full_obj>
)
target_include_directories(test_n148_phase7_epic PRIVATE src)

add_executable(test_n148_roundtrip
    test/test_n148_roundtrip.c
    $<TARGET_OBJECTS:n148_full_obj>
)
target_include_directories(test_n148_roundtrip PRIVATE src)

add_executable(test_n148_interpolation
    test/test_n148_interpolation.c
    src/common/interpolation.c
)
target_include_directories(test_n148_interpolation PRIVATE src)

add_executable(test_n148_reorder
    test/test_n148_reorder.c
    src/encoder/n148/n148_reorder.c
)
target_include_directories(test_n148_reorder PRIVATE src)

add_executable(test_n148_gop_planner
    test/test_n148_gop_planner.c
    src/encoder/n148/n148_reorder.c
)
target_include_directories(test_n148_gop_planner PRIVATE src)

add_executable(test_n148_cabac
    test/test_n148_cabac.c
    $<TARGET_OBJECTS:n148_full_obj>
)
target_include_directories(test_n148_cabac PRIVATE
    src
    src/common/entropy
    src/codec/n148
    src/decoder/n148
    src/encoder/n148
)

add_executable(test_n148_cabac_engine
    test/test_n148_cabac_engine.c
    src/common/entropy/n148_cabac_engine.c
    src/common/entropy/n148_cabac_tables.c
    src/codec/n148/n148_bitstream.c
)
target_include_directories(test_n148_cabac_engine PRIVATE src)

add_executable(test_n148_cabac_slice_roundtrip
    test/test_n148_cabac_slice_roundtrip.c
    $<TARGET_OBJECTS:n148_full_obj>
)
target_include_directories(test_n148_cabac_slice_roundtrip PRIVATE src)

add_executable(test_n148_cabac_conformance_small
    test/test_n148_cabac_conformance_small.c
    $<TARGET_OBJECTS:n148_full_obj>
)
target_include_directories(test_n148_cabac_conformance_small PRIVATE src)

# ===========================================================================
# FASE 8 Tests — Rate Control, Profiles, Metrics
# ===========================================================================

add_executable(test_n148_ratecontrol
    test/test_n148_ratecontrol.c
    src/encoder/n148/n148_ratecontrol.c
)
target_include_directories(test_n148_ratecontrol PRIVATE src)
if(NOT WIN32)
    target_link_libraries(test_n148_ratecontrol PRIVATE m)
endif()

add_executable(test_n148_metrics
    test/test_n148_metrics.c
    src/common/n148_metrics.c
)
target_include_directories(test_n148_metrics PRIVATE src)
if(NOT WIN32)
    target_link_libraries(test_n148_metrics PRIVATE m)
endif()

add_executable(test_n148_profiles
    test/test_n148_profiles.c
    src/encoder/n148/n148_profiles.c
)
target_include_directories(test_n148_profiles PRIVATE src)

# --- FASE 9 tools test ---
add_executable(test_n148_tools
    test/test_n148_tools.c
    $<TARGET_OBJECTS:n148_full_obj>
    src/common/n148_metrics.c
)
target_include_directories(test_n148_tools PRIVATE src)
if(NOT WIN32)
    target_link_libraries(test_n148_tools PRIVATE m)
endif()

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

# --- FASE 9: N.148 toolchain ---
add_executable(n148dump
    tools/n148dump.c
    src/codec/n148/n148_bitstream.c
    src/codec/n148/n148_codec_private.c
    src/codec/n148/n148_codec.c
    src/decoder/n148/n148_parser.c
    src/buffered_io.c
)
target_include_directories(n148dump PRIVATE src)

add_executable(n148inspect
    tools/n148inspect.c
    src/codec/n148/n148_bitstream.c
    src/codec/n148/n148_codec_private.c
    src/codec/n148/n148_codec.c
    src/decoder/n148/n148_parser.c
    src/common/entropy/n148_cavlc.c
    src/common/entropy/n148_binarization.c
    src/common/entropy/n148_cabac.c
    src/common/entropy/n148_cabac_engine.c
    src/common/entropy/n148_cabac_contexts.c
    src/common/entropy/n148_cabac_binarization.c
    src/common/entropy/n148_cabac_syntax.c
    src/common/entropy/n148_cabac_tables.c
    src/buffered_io.c
)
target_include_directories(n148inspect PRIVATE src)

add_executable(n148diff
    tools/n148diff.c
    src/common/n148_metrics.c
)
target_include_directories(n148diff PRIVATE src)
if(NOT WIN32)
    target_link_libraries(n148diff PRIVATE m)
endif()

add_executable(n148enc
    tools/n148enc.c
    $<TARGET_OBJECTS:n148_full_obj>
)
target_include_directories(n148enc PRIVATE src)