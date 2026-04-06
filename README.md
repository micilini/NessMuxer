# NessMuxer

![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-Windows%20|%20Linux%20|%20macOS-blue)
![Language](https://img.shields.io/badge/language-C11-orange)

**Standalone C library: raw NV12 frames in → playable `.mkv` file out.**

NessMuxer is a cross-platform C library that receives raw NV12 video frames, encodes them to H.264, and muxes the result into a valid Matroska (`.mkv`) container.

It has no mandatory external dependencies at runtime. Some optional encoder backends may require platform SDKs, system frameworks, or third-party libraries during build and/or distribution.

The caller only needs three functions: **Open → WriteFrame → Close**.

---

## What does it do?

NessMuxer takes the raw pixel data that comes out of screen capture APIs (like Windows Graphics Capture, PipeWire, or AVFoundation) or camera devices and turns it into a compressed, playable video file:

```
NV12 raw frame → H.264 encoding → MKV container (custom EBML muxer) → .mkv file
```

Your application sends uncompressed frames. NessMuxer gives you back a video file that plays in VLC, MPC-HC, ffplay, or any modern player.

---

## Features

- **Cross-platform** — runs on Windows, Linux, and macOS
- **Multiple encoder backends** — Media Foundation, NVENC, libx264, VideoToolbox
- **Smart AUTO mode** — runtime probe detects the best available encoder automatically
- **3-function API** — `Open`, `WriteFrame`, `Close`. That's it
- **Real-time encoding** — designed for screen recording and live capture scenarios
- **Valid MKV output** — includes SeekHead, Cues (seek index), and proper Duration
- **CLI tools** — converter (`nessmux`), validator (`nessmux_validate`), and benchmark (`nessmux_bench`)
- **P/Invoke ready** — clean C API, works directly from C#, Rust, Python, etc.
- **~3000 lines of C** — small, auditable, no bloat

---

## Platform & Encoder Matrix

| Platform | Default Encoder | Optional Encoders | HW Accel |
|---|---|---|---|
| Windows 10/11 | Media Foundation | NVENC, libx264 | Yes (Intel QSV, AMD AMF via MF) |
| Linux | libx264 | NVENC | CPU (or GPU via NVENC) |
| macOS | VideoToolbox | libx264 | Yes (Apple Silicon / Intel) |

When `encoder_type = AUTO`, NessMuxer probes each backend at runtime and picks the best one available: NVENC → VideoToolbox → Media Foundation → libx264.

---

## Optional Dependencies

NessMuxer itself is MIT-licensed, but some encoder backends depend on third-party components with their own licenses and distribution terms.

### Backends that do not require third-party redistribution

- **Media Foundation** — available on supported Windows versions
- **VideoToolbox** — available on macOS

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
# Default (Media Foundation only):
cmake -B build -A x64
cmake --build build --config Release

# With NVENC (requires NVIDIA Video Codec SDK headers downloaded separately):
cmake -B build -A x64 ^
  -DNESS_USE_NVENC=ON ^
  -DNESS_NVENC_INCLUDE_DIR="C:/SDKs/NVIDIA/VideoCodecSDK/Interface"
cmake --build build --config Release

# With libx264 (requires x264 headers/libraries obtained separately):
cmake -B build -A x64 ^
  -DNESS_USE_X264=ON ^
  -DNESS_X264_INCLUDE_DIR="C:/SDKs/x264/include"
cmake --build build --config Release

# Everything:
cmake -B build -A x64 ^
  -DNESS_USE_NVENC=ON ^
  -DNESS_NVENC_INCLUDE_DIR="C:/SDKs/NVIDIA/VideoCodecSDK/Interface" ^
  -DNESS_USE_X264=ON ^
  -DNESS_X264_INCLUDE_DIR="C:/SDKs/x264/include"
cmake --build build --config Release
```

Or use the automated script:
```bash
build_and_test.bat          # Media Foundation only
build_and_test.bat nvenc    # + NVENC
build_and_test.bat x264     # + libx264
build_and_test.bat all      # Everything
```

### Linux

**Requirements:** GCC/Clang, CMake 3.15+

```bash
# Ubuntu / Debian example for libx264:
sudo apt install build-essential cmake pkg-config libx264-dev

cmake -B build -DNESS_USE_X264=ON
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
# VideoToolbox (ships with macOS):
cmake -B build
cmake --build build

# With libx264:
brew install x264
cmake -B build -DNESS_USE_X264=ON
cmake --build build
```

---

## Third-party setup

This repository does **not** redistribute proprietary SDK materials or GPL third-party code inside `third_party/`.

### NVIDIA Video Codec SDK (NVENC)

1. Go to the official NVIDIA Video Codec SDK page
2. Accept the SDK terms if required
3. Download the SDK package
4. Use the header directory from the SDK in your CMake configuration

Example local path on Windows:

```text
C:/SDKs/NVIDIA/VideoCodecSDK/Interface
```

### x264

You have two common options:

1. **Linux/macOS package manager route**

   * Install x264 from your system package manager (`apt`, `brew`, etc.)
2. **Manual source route**

   * Download the official x264 source from VideoLAN
   * Build it locally
   * Point NessMuxer to your local `include` directory

Example local path:

```text
C:/SDKs/x264/include
```

---

## CLI Tools

### nessmux — Raw-to-MKV Converter

```bash
nessmux input.raw output.mkv --width 1920 --height 1080 --fps 30 --bitrate 6000
nessmux input.raw output.mkv --width 1920 --height 1080 --fps 30 --bitrate 6000 --encoder nvenc
```

Supported `--encoder` values: `auto`, `mf`, `x264`, `nvenc`, `vt` (VideoToolbox)

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

### nessmux_bench — Encoder Benchmark

```bash
nessmux_bench                          # all presets, auto encoder
nessmux_bench --encoder nvenc          # benchmark specific encoder
nessmux_bench --width 1920 --height 1080 --frames 300 --encoder x264
```

---

## Usage (C)

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
| `ness_muxer_encoded_count(muxer)` | Number of H.264 packets written to MKV |

### Config

| Field | Type | Description |
|---|---|---|
| `output_path` | `const char*` | Path to the output `.mkv` file |
| `width` | `int` | Frame width in pixels (must be even) |
| `height` | `int` | Frame height in pixels (must be even) |
| `fps` | `int` | Framerate (e.g., 30) |
| `bitrate_kbps` | `int` | H.264 bitrate in kbps (e.g., 6000 for 1080p) |
| `encoder_type` | `int` | 0=AUTO, 1=MediaFoundation, 2=x264, 3=NVENC, 4=VideoToolbox |

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
│   │   └── vtbox_encoder.c           ← macOS VideoToolbox hardware encoder
│   ├── mf_encoder.h/.c               ← Media Foundation IMFTransform (Windows)
│   ├── mkv_muxer.h/.c                ← MKV container writer (EBML/Matroska)
│   ├── ebml_writer.h/.c              ← EBML primitive writers
│   ├── avc_utils.h/.c                ← H.264 NALU parsing, Annex-B ↔ AVCC
│   ├── buffered_io.h/.c              ← Buffered file I/O layer (cross-platform)
│   └── mkv_defs.h                    ← Matroska element IDs
├── tools/
│   ├── nessmux.c                     ← Raw-to-MKV CLI converter
│   ├── nessmux_validate.c            ← MKV structure validator
│   └── nessmux_bench.c               ← Encoder benchmark
├── test/
│   ├── test_ebml.c                   ← Unit tests: EBML primitives (10 tests)
│   ├── test_avc.c                    ← Unit tests: H.264 utils (8 tests)
│   ├── test_encoder.c                ← Integration: MF encoder (Windows only)
│   ├── test_muxer_only.c             ← Integration: MKV muxer (6 tests)
│   └── test_full_pipeline.c          ← End-to-end: NV12 → MKV (3 checks)
├── build_and_test.bat                ← Automated build/test (Windows)
├── build_and_test.sh                 ← Automated build/test (Linux/macOS)
├── CMakeLists.txt
└── README.md
```

> Optional third-party SDKs/libraries are expected to live outside the repository or in an ignored local `third_party/` folder.

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
    └─ encoder_type == specific?
        └─→ ness_encoder_get(type) → return vtable directly
```

### Internal Pipeline

```
ness_muxer_write_frame(nv12_data)
    │
    ├─→ vtable->submit_frame(nv12_data)      ← NV12 → encoder backend
    │
    ├─→ vtable->receive_packets()             ← Collect H.264 packets
    │       └─→ for each H.264 packet:
    │
    └─→ mkv_muxer_write_packet(h264_data)    ← Write to MKV
            ├─→ avc_annexb_to_mp4()           ← Convert start codes
            ├─→ Cluster management
            └─→ EBML SimpleBlock write
```

---

## Tested Configurations

| OS | Encoder | Status |
|---|---|---|
| Windows 11 (x64) | Media Foundation | ✅ 591 frames, 1080p, valid MKV |
| Windows 11 (x64) | NVENC (RTX GPU) | ✅ 591 frames, 1080p, valid MKV |
| Windows 11 (x64) | libx264 (DLL) | ✅ 591 frames, 1080p, valid MKV |
| Windows 11 (x64) | AUTO (→ NVENC) | ✅ Runtime probe selected NVENC |
| Ubuntu 24.04 (x64) | libx264 | ✅ 591 frames, 1080p, valid MKV |
| Ubuntu 24.04 (x64) | AUTO (→ x264) | ✅ Runtime probe selected x264 |
| macOS | VideoToolbox | 🔧 Implemented, awaiting test hardware |

---

## Validation

The output MKV files are spec-compliant and can be validated with standard tools:

```bash
# Built-in validator:
nessmux_validate output.mkv

# External tools:
ffprobe -v error -show_format -show_streams output.mkv
mkvinfo output.mkv
ffplay output.mkv
```

---

## License

NessMuxer itself is licensed under the [MIT License](LICENSE).

Optional encoder backends may be subject to separate licenses and distribution terms:

- **Media Foundation** ships with supported versions of Windows
- **VideoToolbox** ships with macOS
- **NVIDIA Video Codec SDK / NVENC headers** are provided by NVIDIA under NVIDIA's own license terms
- **x264** is provided by its authors under GPL/commercial licensing terms

This repository does **not** redistribute NVIDIA Video Codec SDK materials or x264 source code/headers.

If you plan to distribute binaries that use optional third-party encoders, review the applicable license terms carefully and obtain legal advice when needed.

---

## Author

Built as part of [NessStudio](https://github.com/micilini/NessStudio) — a Windows 10/11+ screen recording application.