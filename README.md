# NessMuxer

![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-Windows%20|%20Linux%20|%20macOS-blue)
![Language](https://img.shields.io/badge/language-C11-orange)

**Standalone C library: raw NV12 frames in → playable `.mkv` file out.**

NessMuxer is a cross-platform C library that receives raw NV12 video frames, encodes them using H.264 or the proprietary **N.148 codec**, and muxes the result into a valid Matroska (`.mkv`) container.

It has no mandatory external dependencies at runtime. Some optional encoder backends may require platform SDKs, system frameworks, or third-party libraries during build and/or distribution.

The caller only needs three functions: **Open → WriteFrame → Close**.

---

## What does it do?

NessMuxer takes the raw pixel data that comes out of screen capture APIs (like Windows Graphics Capture, PipeWire, or AVFoundation) or camera devices and turns it into a compressed, playable video file:

```
NV12 raw frame → Encoding (H.264 or N.148) → MKV container (custom EBML muxer) → .mkv file
```

Your application sends uncompressed frames. NessMuxer gives you back a video file that plays in VLC, MPC-HC, ffplay, or any modern player (H.264), or in a custom player (N.148).

---

## Features

- **Cross-platform** — runs on Windows, Linux, and macOS
- **Multiple encoder backends** — Media Foundation, NVENC, libx264, VideoToolbox
- **N.148 codec** — proprietary video codec with CABAC and CAVLC entropy coding
- **Smart AUTO mode** — runtime probe detects the best available encoder automatically
- **3-function API** — `Open`, `WriteFrame`, `Close`. That's it
- **Real-time encoding** — designed for screen recording and live capture scenarios
- **Valid MKV output** — includes SeekHead, Cues (seek index), and proper Duration
- **CLI tools** — converter (`nessmux`), validator (`nessmux_validate`), and benchmark (`nessmux_bench`)
- **N.148 tools** — inspector (`n148inspect`), dumper (`n148dump`), encoder (`n148enc`), diff (`n148diff`)
- **P/Invoke ready** — clean C API, works directly from C#, Rust, Python, etc.

---

## Codec Support

### H.264/AVC (Standard)

Industry-standard video codec with broad player compatibility. Supported through multiple encoder backends.

### N.148 (Proprietary)

N.148 is a custom video codec with its own bitstream format, designed for research and experimentation. **It is not compatible with H.264/AVC, H.265/HEVC, or any ISO/ITU standard.**

Key features:
- **Two profiles**: Main (I+P frames, CAVLC) and Epic (I+P+B frames, CABAC)
- **Two entropy coding modes**: CAVLC (faster) and CABAC (better compression)
- **Integer DCT transform** (4×4 and 8×8 blocks)
- **Intra prediction modes**: DC, Horizontal, Vertical, Diagonal, Planar
- **Inter prediction**: Motion estimation with sub-pixel precision (Epic profile)
- **B-frame support**: Bidirectional prediction with reordering (Epic profile)

> **Note:** N.148 files require a compatible decoder. Standard players (VLC, ffplay) will not play N.148 content without the N.148 decoder library or FFmpeg patches.

---

## Platform & Encoder Matrix

| Platform | Default Encoder | Optional Encoders | HW Accel |
|---|---|---|---|
| Windows 10/11 | Media Foundation | NVENC, libx264, N.148 | Yes (Intel QSV, AMD AMF via MF) |
| Linux | libx264 | NVENC, N.148 | CPU (or GPU via NVENC) |
| macOS | VideoToolbox | libx264, N.148 | Yes (Apple Silicon / Intel) |

When `encoder_type = AUTO`, NessMuxer probes each backend at runtime and picks the best one available: NVENC → VideoToolbox → Media Foundation → libx264.

For N.148 encoding, use `encoder_type = NESS_ENCODER_N148` explicitly.

---

## Optional Dependencies

NessMuxer itself is MIT-licensed, but some encoder backends depend on third-party components with their own licenses and distribution terms.

### Backends that do not require third-party redistribution

- **Media Foundation** — available on supported Windows versions
- **VideoToolbox** — available on macOS
- **N.148** — built-in, no external dependencies

### Backends that require extra care

- **NVENC** — requires NVIDIA Video Codec SDK headers at build time and a supported NVIDIA GPU/driver at runtime
- **libx264** — requires x264 headers/libraries and is subject to GPL/commercial licensing terms from its authors

### Where to get them

- **NVIDIA Video Codec SDK / NVENC headers**
  Download from the official NVIDIA Video Codec SDK page.

- **x264 source / headers**
  Download from the official x264 project page maintained by VideoLAN.

### Suggested local layout

These dependencies are **not** shipped in this repository. A common local layout is:

```text
third_party/
  nvenc/
    include/
  x264/
    include/
    lib/
```

You should obtain these files yourself and point CMake to your local paths.

---

## Quick Start

### Windows

**Requirements:** Visual Studio Build Tools with C++ workload, CMake 3.15+

```bash
# Default (Media Foundation + N.148):
cmake -B build -A x64 -DNESS_USE_N148=ON
cmake --build build --config Release

# With NVENC (requires NVIDIA Video Codec SDK headers downloaded separately):
cmake -B build -A x64 ^
  -DNESS_USE_N148=ON ^
  -DNESS_USE_NVENC=ON ^
  -DNESS_NVENC_INCLUDE_DIR="C:/SDKs/NVIDIA/VideoCodecSDK/Interface"
cmake --build build --config Release

# With libx264 (requires x264 headers/libraries obtained separately):
cmake -B build -A x64 ^
  -DNESS_USE_N148=ON ^
  -DNESS_USE_X264=ON ^
  -DNESS_X264_INCLUDE_DIR="C:/SDKs/x264/include"
cmake --build build --config Release

# Everything:
cmake -B build -A x64 ^
  -DNESS_USE_N148=ON ^
  -DNESS_USE_NVENC=ON ^
  -DNESS_NVENC_INCLUDE_DIR="C:/SDKs/NVIDIA/VideoCodecSDK/Interface" ^
  -DNESS_USE_X264=ON ^
  -DNESS_X264_INCLUDE_DIR="C:/SDKs/x264/include"
cmake --build build --config Release
```

Or use the automated script:
```bash
build_and_test.bat          # Media Foundation + N.148
build_and_test.bat nvenc    # + NVENC
build_and_test.bat x264     # + libx264
build_and_test.bat all      # Everything
```

### Linux

**Requirements:** GCC/Clang, CMake 3.15+

```bash
# Ubuntu / Debian example with libx264:
sudo apt install build-essential cmake pkg-config libx264-dev

cmake -B build -DNESS_USE_X264=ON -DNESS_USE_N148=ON
cmake --build build -j$(nproc)
```

For NVENC on Linux, install the NVIDIA driver/runtime required by your GPU and obtain the NVIDIA Video Codec SDK headers separately.

Or use the automated script:
```bash
chmod +x build_and_test.sh
./build_and_test.sh
./build_and_test.sh --raw /path/to/screen.raw    # with CLI tests
```

### macOS

**Requirements:** Xcode Command Line Tools, CMake 3.15+

```bash
# VideoToolbox (ships with macOS) + N.148:
cmake -B build -DNESS_USE_N148=ON
cmake --build build

# With libx264:
brew install x264
cmake -B build -DNESS_USE_X264=ON -DNESS_USE_N148=ON
cmake --build build
```

---

## CLI Tools

### nessmux — Raw-to-MKV Converter

```bash
# H.264 encoding (default)
nessmux input.raw output.mkv --width 1920 --height 1080 --fps 30 --bitrate 6000

# H.264 with specific encoder
nessmux input.raw output.mkv --width 1920 --height 1080 --fps 30 --bitrate 6000 --encoder nvenc

# N.148 encoding with CAVLC (faster)
nessmux input.raw output.mkv --width 1920 --height 1080 --fps 30 --bitrate 6000 --encoder n148 --codec n148 --entropy cavlc

# N.148 encoding with CABAC (better compression)
nessmux input.raw output.mkv --width 1920 --height 1080 --fps 30 --bitrate 6000 --encoder n148 --codec n148 --entropy cabac
```

**Options:**

| Option | Values | Description |
|---|---|---|
| `--encoder` | `auto`, `mf`, `x264`, `nvenc`, `vt`, `n148` | Encoder backend |
| `--codec` | `avc`, `n148` | Output codec format |
| `--entropy` | `cavlc`, `cabac` | Entropy coding mode (N.148 only) |

### nessmux_validate — MKV Validator

```bash
nessmux_validate output.mkv
```

```
[OK] EBML Header: version=1, doctype=matroska, doctypeversion=4
[OK] Segment found at offset 59
[OK] SeekHead: 3 entries
[OK] Info: TimestampScale=1000000, Duration=19699.33ms
[OK] Tracks: 1 track(s)
     Track 1: video, V_MPEG4/ISO/AVC, 1920x1080
     CodecPrivate: 39 bytes (AVCC v1, profile=66, level=40)
[OK] Clusters: 20 clusters, 591 SimpleBlocks
[OK] Cues: 10 entries
[OK] VALID — 591 frames, 19.70s, 10 keyframes
```

For N.148 files:
```
     Track 1: video, V_NESS/N148, 1920x1080
     CodecPrivate: 32 bytes (N.148 Sequence Header)
```

### nessmux_bench — Encoder Benchmark

```bash
nessmux_bench                                    # all presets, auto encoder
nessmux_bench --encoder nvenc                    # benchmark specific encoder
nessmux_bench --encoder n148                     # benchmark N.148 encoder
nessmux_bench --width 1920 --height 1080 --frames 300 --encoder x264
```

### N.148 Tools

#### n148inspect — Bitstream Inspector

```bash
n148inspect input.mkv              # Inspect N.148 bitstream from MKV
n148inspect input.n148             # Inspect raw N.148 bitstream
```

Shows detailed information about NAL units, sequence headers, frame headers, and slice data.

#### n148dump — Frame Dumper

```bash
n148dump input.mkv output.nv12     # Decode and dump frames to raw NV12
n148dump input.mkv --info          # Show stream information only
```

#### n148enc — Standalone Encoder

```bash
n148enc input.nv12 output.n148 --width 1920 --height 1080 --fps 30 --profile epic --entropy cabac
```

#### n148diff — Bitstream Comparator

```bash
n148diff file1.mkv file2.mkv       # Compare two N.148 bitstreams
```

---

## Usage (C)

### H.264 Encoding

```c
#include "ness_muxer.h"

NessMuxer* muxer = NULL;
NessMuxerConfig config = {0};
config.output_path  = "output.mkv";
config.width        = 1920;
config.height       = 1080;
config.fps          = 30;
config.bitrate_kbps = 6000;
config.encoder_type = NESS_ENCODER_AUTO;  /* picks best available */
config.codec_type   = NESS_CODEC_AVC;     /* H.264 output */

ness_muxer_open(&muxer, &config);

while (capturing) {
    uint8_t* nv12_frame = get_frame_from_capture();
    int frame_size = 1920 * 1080 * 3 / 2;
    ness_muxer_write_frame(muxer, nv12_frame, frame_size);
}

ness_muxer_close(muxer);
```

### N.148 Encoding

```c
#include "ness_muxer.h"

NessMuxer* muxer = NULL;
NessMuxerConfig config = {0};
config.output_path  = "output.mkv";
config.width        = 1920;
config.height       = 1080;
config.fps          = 30;
config.bitrate_kbps = 6000;
config.encoder_type = NESS_ENCODER_N148;   /* N.148 encoder */
config.codec_type   = NESS_CODEC_N148;     /* N.148 output */
config.entropy_mode = NESS_ENTROPY_CABAC;  /* CABAC (or NESS_ENTROPY_CAVLC) */

ness_muxer_open(&muxer, &config);

while (capturing) {
    uint8_t* nv12_frame = get_frame_from_capture();
    int frame_size = 1920 * 1080 * 3 / 2;
    ness_muxer_write_frame(muxer, nv12_frame, frame_size);
}

ness_muxer_close(muxer);
```

## Usage (C# / P/Invoke)

```csharp
[DllImport("NessMuxer", CallingConvention = CallingConvention.Cdecl)]
static extern int ness_muxer_open(out IntPtr muxer, ref NessMuxerConfig config);

[DllImport("NessMuxer", CallingConvention = CallingConvention.Cdecl)]
static extern int ness_muxer_write_frame(IntPtr muxer, IntPtr nv12_data, int nv12_size);

[DllImport("NessMuxer", CallingConvention = CallingConvention.Cdecl)]
static extern int ness_muxer_close(IntPtr muxer);
```

---

## API Reference

| Function | Description |
|---|---|
| `ness_muxer_open(muxer, config)` | Initialize encoder + muxer. MKV header is written after the first keyframe |
| `ness_muxer_write_frame(muxer, nv12, size)` | Send one NV12 frame. Encoding + muxing happens internally |
| `ness_muxer_close(muxer)` | Drain encoder, write Cues/SeekHead, patch Duration, close file |
| `ness_muxer_error(muxer)` | Get last error message |
| `ness_muxer_frame_count(muxer)` | Number of frames submitted |
| `ness_muxer_encoded_count(muxer)` | Number of encoded packets written to MKV |

### Config

| Field | Type | Description |
|---|---|---|
| `output_path` | `const char*` | Path to the output `.mkv` file |
| `width` | `int` | Frame width in pixels (must be even) |
| `height` | `int` | Frame height in pixels (must be even) |
| `fps` | `int` | Framerate (e.g., 30) |
| `bitrate_kbps` | `int` | Target bitrate in kbps (e.g., 6000 for 1080p) |
| `encoder_type` | `int` | 0=AUTO, 1=MediaFoundation, 2=x264, 3=NVENC, 4=VideoToolbox, 148=N.148 |
| `codec_type` | `int` | 1=AVC/H.264, 148=N.148 |
| `entropy_mode` | `int` | 0=CAVLC (default), 1=CABAC (N.148 only) |

### Encoder Types

| Enum | Value | Description |
|---|---|---|
| `NESS_ENCODER_AUTO` | 0 | Auto-detect best available encoder |
| `NESS_ENCODER_MEDIA_FOUNDATION` | 1 | Windows Media Foundation (H.264) |
| `NESS_ENCODER_X264` | 2 | libx264 software encoder (H.264) |
| `NESS_ENCODER_NVENC` | 3 | NVIDIA hardware encoder (H.264) |
| `NESS_ENCODER_VIDEOTOOLBOX` | 4 | macOS VideoToolbox (H.264) |
| `NESS_ENCODER_N148` | 148 | N.148 software encoder |

### Codec Types

| Enum | Value | Description |
|---|---|---|
| `NESS_CODEC_AVC` | 1 | H.264/AVC (standard, plays everywhere) |
| `NESS_CODEC_N148` | 148 | N.148 (proprietary, requires N.148 decoder) |

### Entropy Modes (N.148 only)

| Enum | Value | Description |
|---|---|---|
| `NESS_ENTROPY_CAVLC` | 0 | Context-Adaptive Variable-Length Coding (faster) |
| `NESS_ENTROPY_CABAC` | 1 | Context-Adaptive Binary Arithmetic Coding (better compression) |

### Return Codes

| Code | Value | Meaning |
|---|---|---|
| `NESS_OK` | 0 | Success |
| `NESS_ERROR` | -1 | Generic error |
| `NESS_ERROR_IO` | -2 | File I/O error |
| `NESS_ERROR_PARAM` | -3 | Invalid parameter |
| `NESS_ERROR_STATE` | -4 | Invalid state |
| `NESS_ERROR_ENCODER` | -5 | Encoder backend error |
| `NESS_ERROR_ALLOC` | -6 | Memory allocation error |

---

## Architecture

```
NessMuxer/
├── include/
│   └── ness_muxer.h                  ← Public API (the only header consumers need)
├── src/
│   ├── ness_muxer.c                  ← Orchestrator: open/write_frame/close
│   ├── encoder/
│   │   ├── encoder.h                 ← Encoder interface (vtable)
│   │   ├── encoder.c                 ← Backend dispatcher + runtime probe
│   │   ├── mf_encoder_backend.c      ← Windows Media Foundation wrapper
│   │   ├── x264_encoder.c            ← libx264 software encoder
│   │   ├── nvenc_encoder.c           ← NVIDIA NVENC hardware encoder
│   │   ├── vtbox_encoder.c           ← macOS VideoToolbox hardware encoder
│   │   └── n148/                     ← N.148 encoder
│   │       ├── n148_encoder.c        ← N.148 encoder core
│   │       ├── n148_intra.c          ← Intra prediction
│   │       ├── n148_inter.c          ← Inter prediction (P/B frames)
│   │       ├── n148_transform.c      ← DCT transform
│   │       ├── n148_quant.c          ← Quantization
│   │       ├── n148_reorder.c        ← Frame reordering (B-frames)
│   │       └── n148_ratecontrol.c    ← Bitrate control
│   ├── decoder/
│   │   └── n148/                     ← N.148 decoder
│   │       ├── n148_decoder.c        ← N.148 decoder core
│   │       ├── n148_parser.c         ← Bitstream parser
│   │       └── n148_dpb.c            ← Decoded picture buffer
│   ├── codec/
│   │   ├── avc/                      ← H.264/AVC utilities
│   │   └── n148/                     ← N.148 codec common code
│   │       ├── n148_bitstream.c      ← Bitstream reader/writer
│   │       └── n148_spec.h           ← N.148 specification constants
│   ├── common/
│   │   └── entropy/                  ← Entropy coding
│   │       ├── n148_cabac.c          ← CABAC encoder/decoder
│   │       ├── n148_cabac_engine.c   ← CABAC arithmetic engine
│   │       ├── n148_cabac_contexts.c ← Context modeling
│   │       └── n148_cavlc.c          ← CAVLC encoder/decoder
│   ├── mf_encoder.h/.c               ← Media Foundation IMFTransform (Windows)
│   ├── mkv_muxer.h/.c                ← MKV container writer (EBML/Matroska)
│   ├── ebml_writer.h/.c              ← EBML primitive writers
│   ├── avc_utils.h/.c                ← H.264 NALU parsing, Annex-B ↔ AVCC
│   ├── buffered_io.h/.c              ← Buffered file I/O layer (cross-platform)
│   └── mkv_defs.h                    ← Matroska element IDs
├── lib/
│   └── n148dec/                      ← N.148 decoder library (DLL)
│       ├── n148dec.c                 ← Public decoder API
│       └── n148dec.h                 ← Public decoder header
├── tools/
│   ├── nessmux.c                     ← Raw-to-MKV CLI converter
│   ├── nessmux_validate.c            ← MKV structure validator
│   ├── nessmux_bench.c               ← Encoder benchmark
│   ├── n148inspect.c                 ← N.148 bitstream inspector
│   ├── n148dump.c                    ← N.148 frame dumper
│   ├── n148enc.c                     ← Standalone N.148 encoder
│   └── n148diff.c                    ← N.148 bitstream comparator
├── test/
│   ├── test_ebml.c                   ← Unit tests: EBML primitives
│   ├── test_avc.c                    ← Unit tests: H.264 utils
│   ├── test_encoder.c                ← Integration: MF encoder (Windows only)
│   ├── test_muxer_only.c             ← Integration: MKV muxer
│   ├── test_full_pipeline.c          ← End-to-end: NV12 → MKV
│   ├── test_n148_codec.c             ← N.148 codec tests
│   ├── test_n148_encoder.c           ← N.148 encoder tests
│   ├── test_n148_cabac.c             ← CABAC tests
│   ├── test_n148_cabac_engine.c      ← CABAC engine tests
│   └── ...                           ← Additional N.148 tests
├── docs/
│   ├── N148-Bitstream-Spec-v0.1.md   ← N.148 bitstream specification
│   ├── N148-CABAC-Spec-v0.1.md       ← N.148 CABAC specification
│   ├── N148-Container-Mapping-v0.1.md← N.148 MKV mapping
│   └── N148-Test-Vectors.md          ← Test vectors for validation
├── ffmpeg-n148/                      ← FFmpeg patches for N.148 support
│   ├── n148_decoder.c                ← FFmpeg N.148 decoder
│   ├── n148_parser.c                 ← FFmpeg N.148 parser
│   └── README.md                     ← Integration instructions
├── build_and_test.bat                ← Automated build/test (Windows)
├── build_and_test.sh                 ← Automated build/test (Linux/macOS)
├── convert_video.bat                 ← Quick conversion script (Windows)
├── CMakeLists.txt
└── README.md
```

### Encoder Selection Flow

```
ness_muxer_open(config)
    │
    ├─ encoder_type == AUTO?
    │   └─→ ness_encoder_get_best()
    │       ├─ try NVENC    → create(160x120) → success? use it
    │       ├─ try VTBox    → create(160x120) → success? use it
    │       ├─ try MF       → create(160x120) → success? use it
    │       └─ try x264     → create(160x120) → success? use it
    │
    ├─ encoder_type == N148?
    │   └─→ Initialize N.148 encoder
    │       ├─ entropy_mode == CABAC? → Epic profile
    │       └─ entropy_mode == CAVLC? → Main profile
    │
    └─ encoder_type == specific?
        └─→ ness_encoder_get(type) → return vtable directly
```

### Internal Pipeline

```
ness_muxer_write_frame(nv12_data)
    │
    ├─→ vtable->submit_frame(nv12_data)      ← NV12 → encoder backend
    │
    ├─→ vtable->receive_packets()             ← Collect encoded packets
    │       └─→ for each packet:
    │
    └─→ mkv_muxer_write_packet(data)          ← Write to MKV
            ├─→ [AVC] avc_annexb_to_mp4()     ← Convert start codes
            ├─→ [N148] length-prefix NALs    ← MP4-style framing
            ├─→ Cluster management
            └─→ EBML SimpleBlock write
```

---

## N.148 Codec Details

### Profiles

| Profile | ID | Frame Types | Entropy | Motion Estimation |
|---|---|---|---|---|
| Main | 0x01 | I + P | CAVLC | Full-pixel |
| Epic | 0x02 | I + P + B | CABAC | Sub-pixel (quarter-pixel) |

### NAL Unit Types

| Type | Value | Description |
|---|---|---|
| SLICE | 0 | P-frame slice |
| IDR | 1 | I-frame (keyframe) |
| SEQ_HDR | 2 | Sequence header |
| FRM_HDR | 3 | Frame header |
| SEI | 4 | Supplemental information |

### Container Mapping (MKV)

- **CodecID:** `V_NESS/N148`
- **CodecPrivate:** 32-byte Sequence Header
- **NAL framing:** 4-byte length prefix (big-endian), same as H.264 in MP4/MKV

---

## Tested Configurations

| OS | Encoder | Codec | Status |
|---|---|---|---|
| Windows 11 (x64) | Media Foundation | H.264 | ✅ Working |
| Windows 11 (x64) | NVENC (RTX GPU) | H.264 | ✅ Working |
| Windows 11 (x64) | libx264 | H.264 | ✅ Working |
| Windows 11 (x64) | N.148 | N.148 (CAVLC) | ✅ Working |
| Windows 11 (x64) | N.148 | N.148 (CABAC) | ✅ Working |
| Ubuntu 24.04 (x64) | libx264 | H.264 | ✅ Working |
| Ubuntu 24.04 (x64) | N.148 | N.148 | ✅ Working |
| macOS | VideoToolbox | H.264 | 🔧 Implemented |
| macOS | N.148 | N.148 | 🔧 Implemented |

---

## Validation

The output MKV files are spec-compliant and can be validated with standard tools:

```bash
# Built-in validator:
nessmux_validate output.mkv

# External tools (H.264 only):
ffprobe -v error -show_format -show_streams output.mkv
mkvinfo output.mkv
ffplay output.mkv

# N.148 files:
n148inspect output.mkv
n148dump output.mkv --info
```

---

## FFmpeg Integration

The `ffmpeg-n148/` directory contains patches to add N.148 support to FFmpeg:

- `n148_decoder.c` — FFmpeg-compatible decoder
- `n148_parser.c` — NAL unit parser

See `ffmpeg-n148/README.md` for integration instructions.

---

## License

NessMuxer itself is licensed under the [MIT License](LICENSE).

Optional encoder backends may be subject to separate licenses and distribution terms:

- **Media Foundation** ships with supported versions of Windows
- **VideoToolbox** ships with macOS
- **NVIDIA Video Codec SDK / NVENC headers** are provided by NVIDIA under NVIDIA's own license terms
- **x264** is provided by its authors under GPL/commercial licensing terms
- **N.148** is part of NessMuxer and covered by the MIT license

This repository does **not** redistribute NVIDIA Video Codec SDK materials or x264 source code/headers.

If you plan to distribute binaries that use optional third-party encoders, review the applicable license terms carefully and obtain legal advice when needed.

---

## Author

Built as part of [NessStudio](https://github.com/micilini/NessStudio) — a Windows 10/11+ screen recording application.