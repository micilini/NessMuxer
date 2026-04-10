# N.148 Bitstream Specification — v0.1

**Status:** Draft
**Author:** NessMuxer Project
**Date:** 2026-04

---

## 1. Overview

N.148 is a proprietary video codec with its own bitstream format, not compatible with H.264/AVC, H.265/HEVC, or any existing ISO/ITU standard.

The codec operates on planar YCbCr 4:2:0 video with 8-bit depth (extensible to 10-bit in future versions).

### 1.1 Profiles

| Profile ID | Name  | Frame Types | Entropy | Motion       |
|------------|-------|-------------|---------|--------------|
| 0x01       | Main  | I + P       | CAVLC   | Full-pel ME  |
| 0x02       | Epic  | I + P + B   | CABAC   | Sub-pel ME   |

### 1.2 Byte Order

All multi-byte fields are encoded in **big-endian** (network byte order).

---

## 2. NAL Unit Structure

Each N.148 NAL unit follows this format:

```
[start_code: 3 bytes]  0x00 0x00 0x01
[nal_header: 1 byte]
[payload:    N bytes]
```

### 2.1 NAL Header (1 byte)

```
bit 7    : forbidden_zero_bit (must be 0)
bits 6-4 : nal_type (0..7)
bits 3-0 : reserved (must be 0)
```

### 2.2 NAL Types

| nal_type | Value | Description             |
|----------|-------|-------------------------|
| SLICE    | 0     | Normal frame slice (P-frame) |
| IDR      | 1     | IDR slice (I-frame)     |
| SEQ_HDR  | 2     | Sequence Header         |
| FRM_HDR  | 3     | Frame Header            |
| SEI      | 4     | Supplemental info       |
| 5-7      | —     | Reserved                |

### 2.3 Emulation Prevention

Within each NAL payload, the sequences `0x00 0x00 0x00`, `0x00 0x00 0x01`, and `0x00 0x00 0x02` must be avoided. To accomplish this, an emulation prevention byte `0x03` is inserted after `0x00 0x00`:

```
0x00 0x00 0x00  →  0x00 0x00 0x03 0x00
0x00 0x00 0x01  →  0x00 0x00 0x03 0x01
0x00 0x00 0x02  →  0x00 0x00 0x03 0x02
0x00 0x00 0x03  →  0x00 0x00 0x03 0x03
```

---

## 3. Sequence Header (NAL type 2)

The Sequence Header describes the global stream parameters. It is transmitted before the first frame and must precede any IDR.

### 3.1 Binary Layout

| Offset | Size    | Field                | Description                              |
|--------|---------|----------------------|------------------------------------------|
| 0      | 4 bytes | magic                | `0x4E 0x31 0x34 0x38` ("N148")           |
| 4      | 1 byte  | version              | Spec version (1 for v0.1)                |
| 5      | 1 byte  | profile              | 0x01=Main, 0x02=Epic                     |
| 6      | 1 byte  | level                | Level (1-10, defines resolution limits)  |
| 7      | 1 byte  | chroma_format        | 0x01=4:2:0                               |
| 8      | 1 byte  | bit_depth            | 8 or 10                                  |
| 9      | 2 bytes | width                | Width in pixels (BE)                     |
| 11     | 2 bytes | height               | Height in pixels (BE)                    |
| 13     | 4 bytes | timescale            | Time units per second (BE)               |
| 17     | 2 bytes | fps_num              | Frame rate numerator (BE)                |
| 19     | 2 bytes | fps_den              | Frame rate denominator (BE)              |
| 21     | 2 bytes | gop_length           | Default GOP size (BE)                    |
| 23     | 1 byte  | entropy_mode         | 0x01=CAVLC, 0x02=CABAC                   |
| 24     | 1 byte  | max_ref_frames       | Maximum reference frames                 |
| 25     | 1 byte  | max_reorder_depth    | 0=Main, >0=Epic (for B-frames)           |
| 26     | 1 byte  | block_size_flags     | Bitmask: bit0=4x4, bit1=8x8, bit2=16x16  |
| 27     | 2 bytes | feature_flags        | Feature bitmask (reserved)               |
| 29     | 3 bytes | reserved             | Padding for extensibility (0x00)         |

**Total size: 32 bytes fixed.**

### 3.2 Block Size Flags (byte 26)

```
bit 0 : supports 4x4 blocks
bit 1 : supports 8x8 blocks
bit 2 : supports 16x16 blocks
bit 3 : supports 32x32 blocks (future)
bits 4-7 : reserved
```

For Main profile v0.1, the default value is `0x07` (4x4 + 8x8 + 16x16).

---

## 4. Frame Header (NAL type 3)

Each frame starts with a Frame Header NAL, followed by one or more Slice NALs.

### 4.1 Binary Layout

| Offset | Size    | Field              | Description                      |
|--------|---------|--------------------|----------------------------------|
| 0      | 1 byte  | frame_type         | 0x01=I, 0x02=P, 0x03=B           |
| 1      | 4 bytes | frame_number       | Sequential frame number (BE)     |
| 5      | 8 bytes | pts                | Presentation timestamp (BE, HNS) |
| 13     | 8 bytes | dts                | Decode timestamp (BE, HNS)       |
| 21     | 1 byte  | qp_base            | Base QP for the frame (0-51)     |
| 22     | 2 bytes | slice_count        | Number of slices in the frame (BE)|
| 24     | 1 byte  | num_ref_frames     | References used in this frame    |
| 25     | N bytes | ref_frame_indices  | List of indices (1 byte each)    |
| 25+N   | 4 bytes | frame_data_size    | Frame data size (BE)             |

**Size: 29 + num_ref_frames bytes.**

For I-frames, `num_ref_frames = 0`.

---

## 5. Slice Data (NAL type 0 or 1)

Each slice contains a sequence of coded blocks.

### 5.1 Slice Header

| Offset | Size    | Field            | Description                       |
|--------|---------|------------------|-----------------------------------|
| 0      | 2 bytes | first_mb_index   | First macroblock index (BE)       |
| 2      | 2 bytes | mb_count         | Number of macroblocks in slice (BE)|

### 5.2 Macroblock (16×16)

Each 16×16 pixel macroblock is composed of:
- 16 luma 4×4 blocks (or 4 blocks of 8×8)
- 4 chroma-U 4×4 blocks (2×2 in 4:2:0 space)
- 4 chroma-V 4×4 blocks

### 5.3 Block Syntax

For each block, the bitstream contains (coded via CAVLC/Exp-Golomb):

```
block_type          : ue(v) — 0=intra, 1=inter_skip, 2=inter_single

If intra:
  a. intra_mode     : ue(v) — intra prediction mode
  b. has_residual   : u(1)
  c. If has_residual:
     i.   cbp       : ue(v) — coded block pattern
     ii.  qp_delta  : se(v)
     iii. coefficients: coded via CAVLC run-level

If inter (future, Phase 5+):
  a. mvd_x          : se(v)
  b. mvd_y          : se(v)
  c. has_residual   : u(1)
  d. (residual same as intra)
```

### 5.4 Intra Prediction Modes

| Mode ID | Name         | Description                              |
|---------|--------------|------------------------------------------|
| 0       | DC           | Average of neighbors above and left      |
| 1       | Horizontal   | Propagate left neighbors                 |
| 2       | Vertical     | Propagate above neighbors                |
| 3       | Diag Down-Left  | Diagonal down-left                    |
| 4       | Diag Down-Right | Diagonal down-right                   |
| 5       | Planar       | Bilinear interpolation                   |

### 5.5 CAVLC Residual Coding

The transformed and quantized coefficients are coded with:

1. **coeff_token**: Exp-Golomb unsigned — total non-zero coefficients and trailing ones
2. **sign flags**: 1 bit per trailing one
3. **levels**: Exp-Golomb signed for each remaining coefficient
4. **total_zeros**: Exp-Golomb unsigned — total zeros before the last coefficient
5. **run_before**: Exp-Golomb unsigned — zeros before each coefficient (last to first)

---

## 6. Transform

### 6.1 Forward DCT 4×4

The transform uses integer DCT (butterfly-based), without floating-point multiplication, similar to H.264 but with its own matrix.

N.148 4×4 transform matrix:

```
     | 1   1   1   1 |
T =  | 2   1  -1  -2 |
     | 1  -1  -1   1 |
     | 1  -2   2  -1 |
```

`Y = T · X · T^T`

### 6.2 Forward DCT 8×8

Extension of the 4×4 DCT for larger blocks. Used when block_size_flags indicates 8×8 support.

---

## 7. Quantization

### 7.1 QP Range

QP ranges from 0 (least quantization, highest quality) to 51 (most quantization).

### 7.2 Qstep

`Qstep = pow(2.0, (QP - 4) / 6.0)`

### 7.3 Dead-zone Quantization

```
level = sign(coeff) * max(0, (abs(coeff) * quant_scale + offset) >> shift)
```

Where `offset` implements the dead-zone: coefficients close to zero are rounded to zero more aggressively.

### 7.4 Zig-Zag Scan Order (4×4)

```
 0  1  5  6
 2  4  7 12
 3  8 11 13
 9 10 14 15
```

---

## 8. Container Mapping (MKV)

See separate document: `N148-Container-Mapping-v0.1.md`.

---

## 9. Versioning

- **v0.1**: I-frame only, Main profile, CAVLC, 4:2:0, 8-bit
- **v0.2** (planned): P-frames, motion estimation
- **v0.3** (planned): B-frames, Epic profile, CABAC