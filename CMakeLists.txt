cmake_minimum_required(VERSION 3.28)
project(NessMuxer VERSION 2.0.0 LANGUAGES C CXX)
option(NESS_BUILD_SHARED "Build the public shared library instead of a static archive" ON)
option(NESS_BUILD_TESTS "Build standalone test and codec-tool targets" ON)
option(NESS_BUILD_V2_TESTS "Build versioned API qualification tests" OFF)
set(CMAKE_C_STANDARD 11)

# ===========================================================================
# Options — o usuario escolhe quais backends extras quer compilar.
# O backend nativo da plataforma (MF no Windows, VT no macOS) e automatico.
# ===========================================================================

option(NESS_USE_X264  "Enable libx264 software encoder backend" OFF)
option(NESS_USE_NVENC "Enable NVIDIA NVENC hardware encoder backend" OFF)
option(NESS_USE_N148  "Enable N.148 autoral codec" OFF)
option(NESS_USE_X264_V2 "Enable the pinned x264 native V2 backend" OFF)
option(NESS_USE_QSV_V2 "Enable the direct oneVPL D3D11 V2 backend" OFF)
option(NESS_USE_NVENC_V2 "Enable the direct NVENC D3D11 V2 backend" OFF)
option(NESS_USE_AMF_V2 "Enable the direct AMD AMF D3D11 V2 backend" OFF)
option(NESS_USE_MEDIA_FOUNDATION "Include Windows Media Foundation encoders (legacy and V2)" ON)
set(NESS_NVENC_V2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nv-codec-headers/include" CACHE PATH "Pinned NVENC headers")
set(NESS_AMF_V2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amf/include" CACHE PATH "Pinned AMF public include directory")
set(NESS_X264_V2_INCLUDE_DIR "" CACHE PATH "Pinned x264 V2 headers")
set(NESS_VPL_SOURCE_DIR "" CACHE PATH "Pinned Intel VPL dispatcher source")

# Paths so necessarios no Windows (Linux/macOS usam pkg-config / frameworks)
set(NESS_X264_INCLUDE_DIR  "" CACHE PATH "Path to x264 headers (Windows only)")
set(NESS_NVENC_INCLUDE_DIR "" CACHE PATH "Path to NVIDIA Video Codec SDK headers")

include(cmake/BundledEncoders.cmake)
ness_bundled_encoders()

# ===========================================================================
# NessMuxer — shared library
# ===========================================================================

if(NESS_BUILD_SHARED)
    set(NESS_LIBRARY_KIND SHARED)
else()
    set(NESS_LIBRARY_KIND STATIC)
endif()
add_library(NessMuxerV2Core OBJECT src/v2/core.cpp)
target_include_directories(NessMuxerV2Core PUBLIC include PRIVATE src/v2)
target_compile_features(NessMuxerV2Core PUBLIC cxx_std_20)
set_target_properties(NessMuxerV2Core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(NessMuxerV2Core PRIVATE /W4 /WX /EHsc /utf-8)
endif()
add_library(NessMuxer ${NESS_LIBRARY_KIND}
    $<TARGET_OBJECTS:NessMuxerV2Core>
    src/v2/api.cpp
    src/v2/backend_stubs.cpp
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
target_compile_features(NessMuxer PRIVATE cxx_std_20)
if(WIN32 AND NESS_USE_X264_V2)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/native-backends.lock.json" runtime_lock)
    string(JSON expected GET "${runtime_lock}" x264 runtimeSha256)
    set(x264_runtime "${CMAKE_CURRENT_SOURCE_DIR}/third_party/x264-abi164/libx264-164.dll")
    file(SHA256 "${x264_runtime}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Bundled x264 runtime hash mismatch")
    endif()
    add_custom_command(TARGET NessMuxer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${x264_runtime}" "$<TARGET_FILE_DIR:NessMuxer>/libx264-164.dll")
endif()
if(MSVC)
    # Visual Studio shares per-target C/C++ flags. Apply strict new-code flags to
    # individual C++ sources so unrelated legacy C diagnostics remain visible.
    set_source_files_properties(src/v2/mf.cpp src/v2/legacy_bridge.cpp src/v2/api.cpp src/v2/backend_stubs.cpp
        src/v2/unsupported.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX;/EHsc;/utf-8")
endif()

if(NESS_USE_NVENC_V2)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT NESS_NVENC_V2_INCLUDE_DIR)
        message(FATAL_ERROR "NESS_USE_NVENC_V2 requires Windows x64 and pinned NESS_NVENC_V2_INCLUDE_DIR")
    endif()
    target_sources(NessMuxer PRIVATE src/v2/nvenc.cpp)
    target_include_directories(NessMuxer PRIVATE "${NESS_NVENC_V2_INCLUDE_DIR}")
    target_compile_definitions(NessMuxer PRIVATE NESS_HAVE_NVENC_V2)
    target_link_libraries(NessMuxer PRIVATE d3d11 dxgi)
    if(MSVC)
        set_source_files_properties(src/v2/nvenc.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX;/EHsc;/utf-8")
    endif()
endif()
if(NESS_USE_AMF_V2)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT NESS_AMF_V2_INCLUDE_DIR)
        message(FATAL_ERROR "NESS_USE_AMF_V2 requires Windows x64 and pinned NESS_AMF_V2_INCLUDE_DIR")
    endif()
    target_sources(NessMuxer PRIVATE src/v2/amf.cpp)
    target_include_directories(NessMuxer PRIVATE "${NESS_AMF_V2_INCLUDE_DIR}")
    target_compile_definitions(NessMuxer PRIVATE NESS_HAVE_AMF_V2)
    target_link_libraries(NessMuxer PRIVATE d3d11 dxgi)
    if(MSVC)
        set_source_files_properties(src/v2/amf.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX;/EHsc;/utf-8")
    endif()
endif()
if(NESS_USE_QSV_V2)
    if(NOT WIN32 OR NOT NESS_VPL_SOURCE_DIR)
        message(FATAL_ERROR "NESS_USE_QSV_V2 requires Windows and pinned NESS_VPL_SOURCE_DIR")
    endif()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_EXPERIMENTAL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${NESS_VPL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/vpl" EXCLUDE_FROM_ALL)
    target_sources(NessMuxer PRIVATE src/v2/qsv.cpp)
    target_compile_definitions(NessMuxer PRIVATE NESS_HAVE_QSV_V2)
    target_link_libraries(NessMuxer PRIVATE VPL d3d11 dxgi)
    if(MSVC)
        set_source_files_properties(src/v2/qsv.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX;/EHsc;/utf-8")
    endif()
endif()
if(NESS_USE_X264_V2)
    if(NOT NESS_X264_V2_INCLUDE_DIR)
        message(FATAL_ERROR "NESS_USE_X264_V2 requires pinned matching headers/runtime")
    endif()
    target_sources(NessMuxer PRIVATE src/v2/x264.cpp)
    target_include_directories(NessMuxer PRIVATE "${NESS_X264_V2_INCLUDE_DIR}")
    target_compile_definitions(NessMuxer PRIVATE NESS_HAVE_X264_V2)
    target_link_libraries(NessMuxer PRIVATE ${CMAKE_DL_LIBS})
    if(WIN32)
        target_link_libraries(NessMuxer PRIVATE bcrypt)
    endif()
    if(MSVC)
        set_source_files_properties(src/v2/x264.cpp src/v2/backend_stubs.cpp
            PROPERTIES COMPILE_OPTIONS "/W4;/WX;/EHsc;/utf-8")
    endif()
endif()
if(NESS_BUILD_SHARED)
    target_compile_definitions(NessMuxer PRIVATE NESSMUXER_EXPORTS)
else()
    target_compile_definitions(NessMuxer PUBLIC NESSMUXER_STATIC)
endif()

# ---------------------------------------------------------------------------
# Platform-native encoder: Media Foundation (Windows) / VideoToolbox (macOS)
# ---------------------------------------------------------------------------

if(WIN32 AND NESS_USE_MEDIA_FOUNDATION)
    target_sources(NessMuxer PRIVATE
        src/v2/mf.cpp
        src/encoder/mf_encoder_backend.c
        src/mf_encoder.c
    )
    target_link_libraries(NessMuxer PRIVATE
        mfplat mfuuid mf ole32 oleaut32 wmcodecdspuuid d3d11 dxgi
    )
    target_compile_definitions(NessMuxer PRIVATE NESS_HAVE_MEDIA_FOUNDATION)
    target_compile_features(NessMuxer PRIVATE cxx_std_20)
elseif(APPLE)
    target_sources(NessMuxer PRIVATE
        src/v2/unsupported.cpp
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
    target_sources(NessMuxer PRIVATE src/v2/unsupported.cpp)
    if(NOT WIN32)
        # Linux needs libm for math functions.
        target_link_libraries(NessMuxer PRIVATE m)
    endif()
endif()
if(WIN32)
    # The legacy worker delegates to V2 and imports no Media Foundation API.
    # It must remain present to diagnose disabled legacy MF requests correctly.
    target_sources(NessMuxer PRIVATE src/v2/legacy_bridge.cpp)
endif()
if(WIN32 AND NESS_BUILD_SHARED)
    target_sources(NessMuxer PRIVATE src/v2/exports.def)
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
# CABAC implementation modules.
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

        # Modular CABAC encoder and decoder
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
        src/encoder/n148/n148_slice_plan.c
    )

    add_subdirectory(lib/n148dec)
endif()

# ===========================================================================
# Tests
# ===========================================================================

if(NESS_BUILD_TESTS OR NESS_BUILD_V2_TESTS)
    include(CTest)
endif()
if(NESS_BUILD_V2_TESTS)
    add_executable(ness_v2_core test/v2/core_tests.cpp)
    target_link_libraries(ness_v2_core PRIVATE NessMuxerV2Core)
    target_include_directories(ness_v2_core PRIVATE src/v2)
    foreach(scope collector codec-buffer io timestamps annex-padding)
        add_test(NAME v2-${scope} COMMAND ness_v2_core ${scope})
    endforeach()
endif()
if(NESS_BUILD_V2_TESTS AND WIN32)
    add_executable(ness_v2_probe test/v2/probe.c)
    target_link_libraries(ness_v2_probe PRIVATE NessMuxer)
    if(NESS_USE_MEDIA_FOUNDATION)
        add_test(NAME v2-software-probe COMMAND ness_v2_probe)
    endif()
    add_executable(ness_v2_api test/v2/api.cpp)
    target_link_libraries(ness_v2_api PRIVATE NessMuxer psapi)
    target_compile_features(ness_v2_api PRIVATE cxx_std_20)
    add_executable(ness_v2_writer_storage test/v2/writer_storage.cpp)
    target_link_libraries(ness_v2_writer_storage PRIVATE NessMuxerV2Core)
    target_include_directories(ness_v2_writer_storage PRIVATE src/v2)
    if(MSVC)
        target_compile_options(ness_v2_writer_storage PRIVATE /W4 /WX /EHsc /utf-8)
    endif()
    add_test(NAME v2-writer-allocation COMMAND ness_v2_writer_storage allocation)
    add_test(NAME v2-writer-boundaries COMMAND ness_v2_writer_storage boundaries)
    add_executable(ness_v2_muxer_only test/v2/remux.cpp)
    target_link_libraries(ness_v2_muxer_only PRIVATE NessMuxerV2Core)
    if(NESS_USE_MEDIA_FOUNDATION)
        foreach(scope api eos lifetime)
            add_test(NAME v2-${scope} COMMAND ness_v2_api ${scope})
        endforeach()
        add_test(NAME v2-encode-rational COMMAND ness_v2_api encode "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>" 30000 1001 90)
        add_test(NAME v2-encode-30 COMMAND ness_v2_api encode "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>" 30 1 60)
    else()
        add_executable(ness_v2_mf_disabled test/v2/mf_disabled.cpp)
        target_link_libraries(ness_v2_mf_disabled PRIVATE NessMuxer d3d11 dxgi)
        target_compile_features(ness_v2_mf_disabled PRIVATE cxx_std_20)
        target_compile_options(ness_v2_mf_disabled PRIVATE /W4 /WX /EHsc /utf-8)
        add_test(NAME v2-media-foundation-disabled COMMAND ness_v2_mf_disabled)
    endif()
    if(NESS_USE_X264_V2)
        target_compile_definitions(ness_v2_api PRIVATE NESS_TEST_X264_V2)
        add_executable(ness_v2_x264 test/v2/x264.cpp)
        target_link_libraries(ness_v2_x264 PRIVATE NessMuxer)
        target_compile_features(ness_v2_x264 PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(ness_v2_x264 PRIVATE /W4 /WX /EHsc /utf-8)
        endif()
        add_test(NAME v2-x264-owned-input-and-drain COMMAND ness_v2_x264
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/x264-owned-input.mkv")
        add_executable(ness_v2_x264_thread_budget test/v2/x264_thread_budget.cpp)
        target_include_directories(ness_v2_x264_thread_budget PRIVATE src/v2)
        target_compile_features(ness_v2_x264_thread_budget PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(ness_v2_x264_thread_budget PRIVATE /W4 /WX /EHsc /utf-8)
        endif()
        add_test(NAME v2-x264-thread-budget COMMAND ness_v2_x264_thread_budget)
    endif()
    if(NESS_USE_NVENC_V2 OR NESS_USE_AMF_V2)
        add_executable(ness_v2_native_gpu test/v2/native_gpu.cpp)
        target_link_libraries(ness_v2_native_gpu PRIVATE NessMuxer d3d11 dxgi)
        target_compile_features(ness_v2_native_gpu PRIVATE cxx_std_20)
        target_compile_options(ness_v2_native_gpu PRIVATE /W4 /WX /EHsc /utf-8)
        foreach(backend nvenc amf)
            add_test(NAME v2-${backend}-direct-owned-input-and-drain COMMAND ness_v2_native_gpu ${backend}
                "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/${backend}-owned-input.mkv")
            set_tests_properties(v2-${backend}-direct-owned-input-and-drain PROPERTIES SKIP_RETURN_CODE 77 LABELS hardware)
        endforeach()
    endif()
    if(NESS_USE_NVENC_V2)
        add_executable(ness_v2_nvenc_contract test/v2/nvenc_contract.cpp)
        target_include_directories(ness_v2_nvenc_contract PRIVATE "${NESS_NVENC_V2_INCLUDE_DIR}")
        target_link_libraries(ness_v2_nvenc_contract PRIVATE NessMuxerV2Core d3d11 dxgi)
        target_compile_features(ness_v2_nvenc_contract PRIVATE cxx_std_20)
        target_compile_options(ness_v2_nvenc_contract PRIVATE /W4 /WX /EHsc /utf-8)
        add_test(NAME v2-nvenc-driver-contract COMMAND ness_v2_nvenc_contract)
    endif()
    if(NESS_USE_QSV_V2)
        add_executable(ness_v2_qsv test/v2/qsv.cpp)
        target_link_libraries(ness_v2_qsv PRIVATE NessMuxer d3d11 dxgi)
        target_compile_features(ness_v2_qsv PRIVATE cxx_std_20)
        target_compile_options(ness_v2_qsv PRIVATE /W4 /WX /EHsc /utf-8)
        add_test(NAME v2-qsv-direct-owned-input-and-drain COMMAND ness_v2_qsv
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/qsv-owned-input.mkv")
        add_test(NAME v2-qsv-single-pending-progress COMMAND ness_v2_qsv
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/qsv-single-pending.mkv" --single-pending)
        set_tests_properties(v2-qsv-direct-owned-input-and-drain v2-qsv-single-pending-progress PROPERTIES LABELS hardware)
    endif()
    if(MSVC)
        foreach(target ness_v2_api ness_v2_core ness_v2_muxer_only)
            target_compile_options(${target} PRIVATE /W4 /WX /EHsc /utf-8)
        endforeach()
    endif()
endif()

if(NESS_BUILD_TESTS)
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
if(WIN32 AND NESS_USE_MEDIA_FOUNDATION)
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

# Exercise the public library, including the versioned legacy bridge. Keeping a
# second ad-hoc source list here would omit the bridge and optional backend objects.
add_executable(test_full_pipeline test/test_full_pipeline.c)
target_link_libraries(test_full_pipeline PRIVATE NessMuxer)

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
    src/encoder/n148/n148_slice_plan.c
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

add_executable(test_n148_cabac_mux
    test/test_n148_cabac_mux.c
    src/mkv_muxer.c
    src/ebml_writer.c
    src/buffered_io.c
    src/avc_utils.c
    $<TARGET_OBJECTS:n148_full_obj>
)
target_include_directories(test_n148_cabac_mux PRIVATE src)

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
# Rate control, profile and metric tests
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

add_executable(test_n148_slice_plan
    test/test_n148_slice_plan.c
    src/encoder/n148/n148_slice_plan.c
)
target_include_directories(test_n148_slice_plan PRIVATE src)

# Command-line codec integration test
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
    cli/nessmux.c
)
target_include_directories(nessmux PRIVATE include)
target_link_libraries(nessmux PRIVATE NessMuxer)

add_executable(nessmux_validate
    cli/nessmux_validate.c
    src/buffered_io.c
    src/codec/n148/n148_codec_private.c
)
target_include_directories(nessmux_validate PRIVATE src)

add_executable(nessmux_bench
    cli/nessmux_bench.c
)
target_include_directories(nessmux_bench PRIVATE include)
target_link_libraries(nessmux_bench PRIVATE NessMuxer)

add_executable(n148_regression_bench
    cli/n148_regression_bench.c
)
target_include_directories(n148_regression_bench PRIVATE include)
target_link_libraries(n148_regression_bench PRIVATE NessMuxer)

# N.148 command-line tools
if(TARGET n148dec)
add_executable(n148_framegrab
    cli/n148_framegrab.c
)
target_include_directories(n148_framegrab PRIVATE src lib/n148dec)
target_link_libraries(n148_framegrab PRIVATE n148dec)
endif()

add_executable(n148dump
    cli/n148dump.c
    src/codec/n148/n148_bitstream.c
    src/codec/n148/n148_codec_private.c
    src/codec/n148/n148_codec.c
    src/decoder/n148/n148_parser.c
    src/buffered_io.c
)
target_include_directories(n148dump PRIVATE src)

add_executable(n148inspect
    cli/n148inspect.c
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
    cli/n148diff.c
    src/common/n148_metrics.c
)
target_include_directories(n148diff PRIVATE src)
if(NOT WIN32)
    target_link_libraries(n148diff PRIVATE m)
endif()

add_executable(n148enc
    cli/n148enc.c
    $<TARGET_OBJECTS:n148_full_obj>
)
target_include_directories(n148enc PRIVATE src)

foreach(test IN ITEMS test_ebml test_avc test_muxer_only test_n148_codec test_n148_mux
        test_n148_encoder test_n148_cabac_mux test_n148_roundtrip test_n148_interpolation
        test_n148_reorder test_n148_gop_planner test_n148_cabac test_n148_cabac_engine
        test_n148_cabac_slice_roundtrip test_n148_cabac_conformance_small
        test_n148_ratecontrol test_n148_metrics test_n148_profiles test_n148_slice_plan test_n148_tools)
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 120)
endforeach()
if(UNIX AND NOT APPLE)
    target_link_libraries(n148_full_obj PUBLIC m)
    foreach(target test_n148_codec test_n148_mux test_n148_encoder test_n148_cabac_mux
            test_n148_roundtrip test_n148_cabac test_n148_cabac_slice_roundtrip
            test_n148_cabac_conformance_small test_n148_tools n148enc)
        target_link_libraries(${target} PRIVATE m)
    endforeach()
endif()
endif()

include(GNUInstallDirs)
install(TARGETS NessMuxer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/ness_muxer.h include/ness_muxer_v2.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE DESTINATION share/NessMuxer)
install(FILES README.md DESTINATION share/NessMuxer RENAME README.txt)
install(FILES native-backends.lock.json DESTINATION share/NessMuxer)
if(WIN32 AND NESS_USE_X264_V2)
    install(FILES "${x264_runtime}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES third_party/x264-abi164/licenses/COPYING DESTINATION share/NessMuxer/licenses RENAME x264.LICENSE.txt)
    install(FILES third_party/x264-abi164/licenses/AUTHORS DESTINATION share/NessMuxer/licenses RENAME x264.AUTHORS.txt)
    install(FILES third_party/x264-abi164/x264-eaa68fa.source.tar.gz DESTINATION share/NessMuxer/sources)
endif()
if(NESS_USE_NVENC_V2)
    install(FILES third_party/nv-codec-headers/LICENSE.txt DESTINATION share/NessMuxer/licenses RENAME NVENC.LICENSE.txt)
endif()
if(NESS_USE_AMF_V2)
    install(FILES third_party/amf/LICENSE.txt DESTINATION share/NessMuxer/licenses RENAME AMF.LICENSE.txt)
endif()
if(NESS_USE_QSV_V2)
    install(FILES "${NESS_VPL_SOURCE_DIR}/LICENSE" DESTINATION share/NessMuxer/licenses RENAME VPL.LICENSE.txt)
endif()
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
endif()
