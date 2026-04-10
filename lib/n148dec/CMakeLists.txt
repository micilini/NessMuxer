add_library(n148dec SHARED
    n148dec.c

    ../../src/codec/n148/n148_bitstream.c
    ../../src/codec/n148/n148_codec_private.c
    ../../src/codec/n148/n148_codec.c

    ../../src/decoder/n148/n148_decoder.c
    ../../src/decoder/n148/n148_parser.c
    ../../src/decoder/n148/n148_frame_recon.c
    ../../src/decoder/n148/n148_motion.c
    ../../src/decoder/n148/n148_dpb.c

    ../../src/common/interpolation.c

    ../../src/common/entropy/n148_binarization.c
    ../../src/common/entropy/n148_cavlc.c
    ../../src/common/entropy/n148_cabac.c
    ../../src/common/entropy/n148_cabac_engine.c
    ../../src/common/entropy/n148_cabac_contexts.c
    ../../src/common/entropy/n148_cabac_binarization.c
    ../../src/common/entropy/n148_cabac_syntax.c
    ../../src/common/entropy/n148_cabac_tables.c

    ../../src/encoder/n148/n148_quant.c
)

target_include_directories(n148dec
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(n148dec PRIVATE
    N148DEC_EXPORTS
    NESS_HAVE_N148
)

set_target_properties(n148dec PROPERTIES
    OUTPUT_NAME "n148dec"
)