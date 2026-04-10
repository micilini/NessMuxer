# ffmpeg-n148

First package of PHASE 10.2 of NessMuxer / N.148.

## Objective

To serve as a bridge between the external decoder `libn148dec` and the FFmpeg ecosystem.

## What this directory delivers now

- `n148_decoder.c`
  - decoder wrapper that calls `libn148dec`
  - converts N.148 NV12 output into `AVFrame`

- `n148_parser.c`
  - initial passthrough parser
  - designed for the Matroska path, where each MKV block already carries one N.148 access unit

## What is still NOT finalized here

This directory still does not register the codec by itself inside an official FFmpeg build.
For that, it will still be necessary to adjust the FFmpeg source tree.

## Points that need to be touched in FFmpeg

### 1. New codec id
Add an `AV_CODEC_ID_N148` to the FFmpeg codec ID enum.

### 2. Matroska codec mapping
Map `V_NESS/N148` to `AV_CODEC_ID_N148` in the Matroska codec tag table.

### 3. Decoder registration
Register the N.148 decoder in FFmpeg's codec list.

### 4. Parser registration
Register the N.148 parser in FFmpeg's parser list.

## Important note

The current parser was designed as a "passthrough parser" for the first integration path:
- MKV with 1 SimpleBlock = 1 N.148 frame packet

For concatenated elementary `.n148`, a more sophisticated splitter may be added later.

## Recommended order

1. Copy `n148_decoder.c` to the custom codecs area of your FFmpeg fork
2. Copy `n148_parser.c`
3. Create `AV_CODEC_ID_N148`
4. Map `V_NESS/N148`
5. Build custom FFmpeg
6. Only then move on to `ffplay`
