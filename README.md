# NessMuxer

![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-Windows%20|%20Linux%20|%20macOS-blue)
![Language](https://img.shields.io/badge/language-C11%20%2F%20C%2B%2B20-orange)

**Standalone native library with a C API: raw NV12 frames in → playable `.mkv` file out.**

NessMuxer is a cross-platform native library that receives raw NV12 video frames, encodes them using H.264 or the experimental **N.148 codec**, and muxes the result into a Matroska (`.mkv`) container. It exposes C interfaces and contains C11 and C++20 implementation code.

The container and N.148 core can run without an external encoder runtime. H.264 backends require the corresponding system framework, driver or x264 runtime. The Windows preset includes the pinned x264 DLL and enables direct GPU adapters; it does not require Media Foundation.

The original API keeps the **Open → WriteFrame → Close** recording flow, followed by **Destroy** to release the object. The current [V2 API](include/ness_muxer_v2.h) adds separate encoders and writers, owned packets, bounded queues, GPU input leases and explicit drain/finalize operations. Both APIs remain available.

---

## What does it do?

NessMuxer takes the raw pixel data that comes out of screen capture APIs (like Windows Graphics Capture, PipeWire, or AVFoundation) or camera devices and turns it into a compressed, playable video file:

```
NV12 raw frame → Encoding (H.264 or N.148) → MKV container (custom EBML muxer) → .mkv file
```

Your application sends uncompressed frames. NessMuxer gives you back a video file that plays in VLC, MPC-HC, ffplay, or any modern player (H.264), or in a custom player (N.148).

---

## Features

- **Cross-platform core** — Matroska, packet ownership and N.148 build on Windows, Linux and macOS; encoder availability varies by platform.
- **Direct Windows GPU encoding** — Intel Quick Sync through oneVPL, NVIDIA NVENC and AMD AMF with D3D11 input through the V2 API.
- **CPU encoding** — x264 with a pinned ABI/runtime in the Windows preset.
- **Platform encoders** — optional legacy Media Foundation, VideoToolbox and x264 adapters.
- **N.148 codec** — experimental video codec with CABAC and CAVLC entropy coding.
- **Explicit encoder selection** — V2 reports unavailable backends to the host; the original API retains its legacy AUTO selection.
- **C interfaces** — the original frame-to-file API and a versioned encoder/packet/writer API.
- **Recording lifecycle** — input budgets, timestamps, encoder draining and explicit output finalization.
- **MKV output** — SeekHead, Cues (seek index) and Duration.
- **CLI tools** — converter (`nessmux`), validator (`nessmux_validate`) and benchmark (`nessmux_bench`).
- **N.148 tools** — inspector (`n148inspect`), structure dumper (`n148dump`), encoder (`n148enc`), frame extractor (`n148_framegrab`) and raw-frame comparator (`n148diff`).
- **FFI ready** — C headers for C/C++ consumers and bindings in other languages.
- **Automated builds** — GitHub Actions packages for Windows x64, Linux x64 and macOS ARM64.

---

## Codec Support

### H.264/AVC (Standard)

Industry-standard video codec with broad player compatibility. Supported through multiple encoder backends.

### N.148 (Experimental)

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

| Capability | Windows x64 | Linux / macOS |
|---|---|---|
| Matroska/EBML, packets and codec ownership | Yes | Portable core |
| Direct Intel Quick Sync / oneVPL, D3D11 | V2, enabled in Windows preset | Not implemented |
| Direct NVIDIA NVENC, D3D11 | V2, enabled in Windows preset | Not implemented |
| Direct AMD AMF, D3D11 | V2, enabled in Windows preset | Not implemented |
| x264 software encoding | Pinned V2 runtime in Windows preset | External x264 via the optional original API; disabled in portable preset |
| Platform legacy encoders | Optional Media Foundation, disabled in Windows preset | macOS VideoToolbox; optional x264 |
| Experimental N.148 codec and utilities | Enabled in Windows preset | Enabled in portable preset |

**V2 selection is explicit.** The caller requests a backend and handles `NESS_V2_E_UNAVAILABLE`. Automatic policy and fallback across these backends belong to the host, such as libnessstudio. V2 never silently replaces a requested GPU encoder with CPU encoding. Compiling an adapter does not guarantee that the machine has a compatible GPU and driver.

The original `NESS_ENCODER_AUTO` operates on the legacy backend set; it does not select the new V2 QSV/NVENC/AMF adapters. Its vtable dispatcher probes the compiled candidates in order: NVENC → VideoToolbox → Media Foundation → V4L2 → x264. On Windows, AUTO with neither legacy NVENC nor legacy x264 compiled takes the Media Foundation compatibility bridge, which is unavailable when Media Foundation is disabled. Use V2 for H.264 with the supplied `windows-x64` preset.

For N.148 encoding through the original API, use `encoder_type = NESS_ENCODER_N148` and `codec_type = NESS_CODEC_N148` explicitly.

---

## Optional Dependencies

NessMuxer's own code is MIT-licensed. Encoder backends depend on components with their own licenses and runtime requirements.

### System and built-in backends

- **Media Foundation** — optional legacy Windows path; disabled in the supplied Windows preset.
- **VideoToolbox** — system framework on macOS for the original API.
- **N.148** — built into this project when enabled, without an external codec runtime.

### Native H.264 backends

- **Intel Quick Sync** — pinned oneVPL dispatcher source, built statically; encoding requires a supported Intel device and driver runtime.
- **NVENC** — pinned public API headers; encoding requires a supported NVIDIA GPU and installed driver.
- **AMF** — pinned public API headers; encoding requires a supported AMD GPU and installed driver.
- **x264** — matching headers and runtime; the supplied Windows ABI 164 package includes its GPL license and source archive.

### Pinned dependency layout

The dependencies used by the Windows preset are checked into `third_party/` and verified against [`native-backends.lock.json`](native-backends.lock.json):

```text
third_party/
  libvpl-2.17.0.source.zip
  nv-codec-headers/
    include/
    LICENSE.txt
  amf/
    include/
    LICENSE.txt
  x264-abi164/
    libx264-164.dll
    include/
      x264.h
      x264_config.h
    x264-eaa68fa.source.tar.gz
    licenses/
```

Configuring these backends requires no dependency download. GPU drivers come from the operating system or vendor. Keep the installed package's notices and supplied source archive when redistributing the corresponding binaries. Optional legacy x264 builds on Linux/macOS use an external installation found through `pkg-config`.

---

## Quick Start

Clone the repository and run the commands from its root:

```sh
git clone https://github.com/micilini/NessMuxer.git
cd NessMuxer
```

### Windows

**Requirements:** CMake 3.28+, Visual Studio 2026 with the v145 C++ x64 toolset, and a developer shell with CMake on `PATH`.

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel 3
ctest --preset windows-x64 --no-tests=error
cmake --install .out/windows-x64 --config Release --prefix .out/package
```

The preset enables V2 x264/QSV/NVENC/AMF, N.148, tests and CLI tools, and disables Media Foundation. The test preset excludes physical-GPU tests labeled `hardware`.

The install directory contains:

| Path | Contents / deployment |
|---|---|
| `bin/NessMuxer.dll` | Place beside the executable that loads NessMuxer directly |
| `bin/libx264-164.dll` | Keep beside the library when using the bundled x264 backend |
| `lib/NessMuxer.lib` | Import library for native consumers |
| `include/` | Public C headers |
| `share/NessMuxer/` | License notices, dependency lock and supplied source archive |

Keep the package's runtime dependencies and notices together, and use matching x64 Release binaries. The CLI tools remain in `.out/windows-x64/Release`; the install package contains the library SDK.

### Linux

**Requirements:** a C11/C++20 compiler, CMake 3.28+ and Ninja.

```sh
cmake --preset portable
cmake --build --preset portable --parallel 3
ctest --preset portable --no-tests=error
cmake --install .out/portable --prefix .out/package
```

The installed library is `libNessMuxer.so`. The preset builds the portable container/ownership core and N.148, without the Windows direct GPU adapters or x264. To add H.264 to the original API, install x264 development files and `pkg-config`, configure with `cmake --preset portable -DNESS_USE_X264=ON`, then build and install again.

### macOS

**Requirements:** Xcode Command Line Tools, CMake 3.28+ and Ninja.

```sh
cmake --preset portable
cmake --build --preset portable --parallel 3
ctest --preset portable --no-tests=error
cmake --install .out/portable --prefix .out/package
```

The installed library is `libNessMuxer.dylib`. The original API includes the system VideoToolbox adapter. Portable CI validates the core and N.148; it does not qualify physical hardware encoding. An optional original-API x264 build requires an external x264 installation and `pkg-config`, using `-DNESS_USE_X264=ON`.

For static embedding, set `NESS_BUILD_SHARED=OFF`, `NESS_BUILD_TESTS=OFF` and `NESS_BUILD_V2_TESTS=OFF`, then link the `NessMuxer` CMake target so its compile definitions and dependencies propagate. NessStudio uses this arrangement inside `libnessstudio.dll`; it does not load a separate `NessMuxer.dll`.

---

## CLI Tools

Build with `NESS_BUILD_TESTS=ON` (enabled by both presets). Tools are in `.out/windows-x64/Release` on Windows or `.out/portable` with Ninja; add that directory to `PATH`, or invoke each executable by its full path.

`nessmux` and `nessmux_bench` use the **original API**. Their H.264 examples require a configured legacy encoder; enabling `NESS_USE_X264_V2` or a V2 GPU backend alone does not enable those CLI routes. N.148 examples work with the supplied presets. See the final V2 section for current H.264 integrations.

### nessmux — Raw-to-MKV Converter

```bash
# H.264 through a configured legacy encoder
nessmux input.raw output.mkv --width 1920 --height 1080 --fps 30 --bitrate 6000

# H.264 with the optional legacy NVENC backend
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
nessmux_bench                                    # legacy AUTO; requires a legacy encoder
nessmux_bench --encoder nvenc                    # benchmark specific encoder
nessmux_bench --encoder n148                     # benchmark N.148 encoder
nessmux_bench --width 1920 --height 1080 --frames 300 --encoder x264
```

### N.148 Tools

#### n148inspect — Bitstream Inspector

```bash
n148inspect input.mkv              # Inspect N.148 bitstream from MKV
n148inspect input.mkv --summary    # Summarize the stream
```

Shows detailed information about NAL units, sequence headers, frame headers, and slice data.

#### n148dump — Bitstream Structure Dumper

```bash
n148dump input.mkv                # Print NAL and header details from MKV
n148dump input.n148 --raw         # Inspect a raw N.148 bitstream
```

#### n148enc — Standalone Encoder

```bash
n148enc input.nv12 output.n148 --width 1920 --height 1080 --fps 30 --profile epic
```

#### n148diff — Raw Frame Comparator

```bash
n148diff original.nv12 decoded.nv12 --width 1920 --height 1080 --diff difference.nv12
```

---

## Usage (C)

The examples below use [`ness_muxer.h`](include/ness_muxer.h), the original synchronous frame-to-file API. Provide your own `capturing` condition and capture-buffer acquisition. Frames must contain contiguous NV12 data with the configured dimensions; consume each return code. For the modern Windows H.264 backends, use the [V2 API](#version-200--versioned-api-and-native-backends).

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
config.encoder_type = NESS_ENCODER_AUTO;  /* probes configured legacy backends */
config.codec_type   = NESS_CODEC_AVC;     /* H.264 output */

int status = ness_muxer_open(&muxer, &config);
if (status != NESS_OK)
    return status;

while (capturing) {
    const uint8_t* nv12_frame = get_frame_from_capture();
    int frame_size = 1920 * 1080 * 3 / 2;
    status = ness_muxer_write_frame(muxer, nv12_frame, frame_size);
    /* Release/reuse the caller-owned capture buffer after this call. */
    if (status != NESS_OK)
        break;
}

int close_status = ness_muxer_close(muxer);
/* Inspect ness_muxer_error(muxer) and final counters before destroy. */
ness_muxer_destroy(muxer);
muxer = NULL;
return status != NESS_OK ? status : close_status;
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

int status = ness_muxer_open(&muxer, &config);
if (status != NESS_OK)
    return status;

while (capturing) {
    const uint8_t* nv12_frame = get_frame_from_capture();
    int frame_size = 1920 * 1080 * 3 / 2;
    status = ness_muxer_write_frame(muxer, nv12_frame, frame_size);
    /* Release/reuse the caller-owned capture buffer after this call. */
    if (status != NESS_OK)
        break;
}

int close_status = ness_muxer_close(muxer);
/* Inspect ness_muxer_error(muxer) and final counters before destroy. */
ness_muxer_destroy(muxer);
muxer = NULL;
return status != NESS_OK ? status : close_status;
```

## Usage (C# / P/Invoke)

The native entry points remain available for a binding that defines `NessMuxerConfig` with the exact native field order, types and alignment from `ness_muxer.h`:

```csharp
[DllImport("NessMuxer", CallingConvention = CallingConvention.Cdecl)]
static extern int ness_muxer_open(out IntPtr muxer, ref NessMuxerConfig config);

[DllImport("NessMuxer", CallingConvention = CallingConvention.Cdecl)]
static extern int ness_muxer_write_frame(IntPtr muxer, IntPtr nv12_data, int nv12_size);

[DllImport("NessMuxer", CallingConvention = CallingConvention.Cdecl)]
static extern int ness_muxer_close(IntPtr muxer);

[DllImport("NessMuxer", CallingConvention = CallingConvention.Cdecl)]
static extern void ness_muxer_destroy(IntPtr muxer);
```

---

## API Reference

This table describes the original API. V2 uses separate versioned envelopes and handles; its contract is in [`ness_muxer_v2.h`](include/ness_muxer_v2.h) and the final section of this README.

| Function | Description |
|---|---|
| `ness_muxer_open(muxer, config)` | Initialize encoder + muxer. MKV header is written after the first keyframe |
| `ness_muxer_write_frame(muxer, nv12, size)` | Send one NV12 frame. Encoding + muxing happens internally |
| `ness_muxer_write_frame_pts(muxer, nv12, size, pts_hns)` | Submit a frame with an explicit timestamp in 100-nanosecond units |
| `ness_muxer_close(muxer)` | Drain/finalize and close the file; keep the object available for final counters/error inspection |
| `ness_muxer_destroy(muxer)` | Release the owned object after close; accepts `NULL` |
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
| `encoder_type` | `int` | 0=AUTO, 1=MediaFoundation, 2=x264, 3=NVENC, 4=VideoToolbox, 5=V4L2, 148=N.148 |
| `codec_type` | `int` | 1=AVC/H.264, 148=N.148 |
| `entropy_mode` | `int` | 0=CAVLC (default), 1=CABAC (N.148 only) |

### Encoder Types

| Enum | Value | Description |
|---|---|---|
| `NESS_ENCODER_AUTO` | 0 | Probe available legacy encoders |
| `NESS_ENCODER_MEDIA_FOUNDATION` | 1 | Windows Media Foundation (H.264) |
| `NESS_ENCODER_X264` | 2 | libx264 software encoder (H.264) |
| `NESS_ENCODER_NVENC` | 3 | NVIDIA hardware encoder (H.264) |
| `NESS_ENCODER_VIDEOTOOLBOX` | 4 | macOS VideoToolbox (H.264) |
| `NESS_ENCODER_V4L2` | 5 | Legacy enum; unavailable unless a V4L2 backend is compiled |
| `NESS_ENCODER_N148` | 148 | N.148 software encoder |

### Codec Types

| Enum | Value | Description |
|---|---|---|
| `NESS_CODEC_AVC` | 1 | H.264/AVC (standard, plays everywhere) |
| `NESS_CODEC_N148` | 148 | N.148 (experimental, requires N.148 decoder) |

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

```text
NessMuxer/
├── include/
│   ├── ness_muxer.h                  ← Original public C API
│   └── ness_muxer_v2.h               ← Versioned encoder, packet and writer API
├── src/
│   ├── ness_muxer.c                  ← Original frame-to-file orchestrator
│   ├── v2/                          ← Versioned implementation and ownership
│   │   ├── api.cpp, core.cpp         ← Contracts, packets, codecs and writer
│   │   ├── x264.cpp, qsv.cpp         ← CPU x264 and Intel Quick Sync adapters
│   │   ├── nvenc.cpp, amf.cpp        ← Direct NVIDIA and AMD adapters
│   │   └── legacy_bridge.cpp        ← Original Windows API compatibility
│   ├── encoder/                     ← Legacy platform and N.148 encoders
│   │   └── n148/                    ← Prediction, transform, reorder and rate control
│   ├── decoder/n148/                ← N.148 decoder, parser and picture buffer
│   ├── codec/                       ← AVC utilities and N.148 bitstream/spec constants
│   ├── common/entropy/              ← CABAC and CAVLC implementations
│   ├── mkv_muxer.h, mkv_muxer.c      ← Matroska writer
│   ├── ebml_writer.h, ebml_writer.c  ← EBML primitives
│   ├── avc_utils.h, avc_utils.c      ← H.264 NAL parsing and framing
│   └── buffered_io.h, buffered_io.c  ← Buffered file I/O
├── lib/n148dec/                     ← Public N.148 decoder library
├── cli/                             ← Converter, validator and codec utilities
├── test/                            ← Container and codec tests
│   └── v2/                          ← Ownership, encoder, driver and lifecycle tests
├── cmake/                           ← Dependency resolution and integrity checks
├── third_party/                     ← Pinned dependencies and their licenses
├── ffmpeg-n148/                     ← Optional experimental FFmpeg integration sources
├── .github/workflows/               ← Build/test/package automation
├── native-backends.lock.json        ← Dependency versions and SHA-256 values
├── CMakePresets.json                ← Windows x64 and portable configurations
├── CMakeLists.txt
└── README.md
```

### Encoder Selection Flow

The original vtable path uses the flow below. The Windows compatibility bridge described in the platform matrix is an additional route; the V2 GPU and x264 backends are selected explicitly by the host.

```
ness_muxer_open(config)
    │
    ├─ encoder_type == AUTO?
    │   └─→ ness_encoder_get_best()
    │       ├─ try NVENC    → create(160x120) → success? use it
    │       ├─ try VTBox    → create(160x120) → success? use it
    │       ├─ try MF       → create(160x120) → success? use it
    │       ├─ try V4L2  → create(160x120) → success? use it
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

Original vtable-based frame-to-file path:

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

| Configuration | Automated coverage | Physical hardware scope |
|---|---|---|
| Windows x64, `windows-x64` | Build/install, API/ownership, container, N.148, CPU x264 and configured driver-contract tests | GPU-labeled tests excluded from hosted CI |
| Ubuntu 24.04 x64, `portable` | Build/install, portable core, container and N.148 tests | No direct Windows GPU path |
| macOS ARM64, `portable` | Build/install, portable core, container and N.148 tests | No claim of physical VideoToolbox qualification |

The [GitHub Actions runs](https://github.com/micilini/NessMuxer/actions/workflows/build-libraries.yml) publish the result for each commit. Driver-contract tests validate API negotiation without certifying every GPU model. Real GPU tests require the corresponding supported device and installed driver.

---

## Validation

Validate generated MKV files with the built-in tools or external container/player tools:

```bash
# Built-in validator:
nessmux_validate output.mkv

# External tools (H.264 only):
ffprobe -v error -show_format -show_streams output.mkv
mkvinfo output.mkv
ffplay output.mkv

# N.148 files:
n148inspect output.mkv
n148dump output.mkv
```

---

## FFmpeg Integration

The [`ffmpeg-n148/`](ffmpeg-n148) directory contains optional experimental integration sources for adding N.148 decoding to a custom FFmpeg build:

- `n148_decoder.c` — FFmpeg-compatible decoder
- `n148_parser.c` — NAL unit parser

These files are not built by the standard NessMuxer presets and do not modify an installed FFmpeg. Integrating them requires adding the codec/parser registrations and build entries to a compatible FFmpeg source tree and linking the N.148 decoder. Stock FFmpeg does not decode N.148 automatically.

---

## License

NessMuxer's own source, including N.148, is licensed under the [MIT License](LICENSE).

Optional components retain their own terms:

- **Media Foundation / VideoToolbox** — system components provided by Windows/macOS.
- **oneVPL dispatcher and GPU API headers** — retain the upstream notices supplied in `third_party/` and the install package.
- **x264** — the supplied runtime/source package is GPL-2.0-or-later and includes its license and source archive.

This repository **does include** pinned upstream headers, runtimes and source archives used by the Windows preset. The installed `share/NessMuxer` directory preserves the corresponding notices/source material. NessMuxer's MIT license does not relicense these dependencies.

---

## Author

Built as part of [NessStudio](https://github.com/micilini/NessStudio) — a Windows 10/11+ screen recording application.

---

## Version 2.0.0 — Versioned API and Native Backends

The current library keeps the original API and adds [`ness_muxer_v2.h`](include/ness_muxer_v2.h) for applications that need explicit encoder selection, packet ownership and recording lifecycle control. The library version and NessStudio's application version are independent: NessStudio 3.0.0 uses this library inside its native engine.

### Use the V2 API

1. Initialize all envelopes with the exact `sizeof` and `version = NESS_V2_VERSION`; zero reserved fields. Choose an explicit backend, dimensions, frame rate, bitrate and bounded pending-frame budget.
2. Create an encoder through `ness_encoder_create_v2` for CPU input or `ness_encoder_create_gpu_v2` with a compatible D3D11 device for GPU input. Report errors with their stage and message.
3. Submit NV12 CPU planes with `ness_encoder_submit_cpu_v2`. Input is copied before the call returns successfully. GPU submission returns an input lease: observe completion before reusing the surface and release the lease afterward.
4. Retrieve owned packets with `ness_encoder_receive_v2`. `NESS_V2_AGAIN` means no packet is ready yet; `NESS_V2_E_BACKPRESSURE` requires the caller to consume pending output before retrying input.
5. Obtain the codec description, open a writer with `ness_mkv_open_v2`, and write packets through `ness_mkv_write_packet_v2`. Packet/codec views are borrowed only while their owned handles remain alive.
6. Complete with `ness_encoder_drain_v2`, receive until `NESS_V2_END`, then call `ness_mkv_finalize_v2`. Release all handles. Discard/release do not count as successful finalization.

Encoder operations are bound to their creating thread. Timestamps and durations use 100-nanosecond units. Preserve frame IDs, DTS/PTS and ownership contracts rather than passing transient buffers across threads. Error results do not transfer ownership unless the public API says otherwise.

[`test/v2/x264.cpp`](test/v2/x264.cpp) and the other `test/v2` consumers demonstrate the complete encode/receive/drain/write lifecycle. The portable core tests cover bounded queues, packets, clocks and muxing boundaries. V2's Matroska writer currently handles a video track; audio capture/mixing and a complete recording application belong to the host.

### Automated builds and tests

The [GitHub Actions workflow](.github/workflows/build-libraries.yml) builds Windows x64, Linux x64 and macOS ARM64 packages, runs their configured tests and uploads install directories as downloadable artifacts. Actions are pinned to immutable revisions.

The Windows test preset excludes tests labeled `hardware`, because hosted runners cannot certify physical encoder availability. On a compatible Windows machine, run all tests with:

```powershell
ctest --test-dir .out/windows-x64 -C Release --output-on-failure
```

Driver-contract tests exercise API negotiation without claiming that every GPU model was tested. Real GPU tests still require the corresponding device and driver.

### Use with NessStudio

NessMuxer remains an independent repository and library. NessStudio pins its source revision and links it **statically into `libnessstudio.dll`**. Building the application normally restores and builds that pinned dependency automatically. A standalone NessMuxer DLL is for consumers that link this library directly; replacing such a DLL beside NessStudio does not update its statically linked engine.

See the [NessStudio V3 build and deployment guide](https://github.com/micilini/NessStudio#version-300--native-recording-engine) and the [libnessstudio SDK guide](https://github.com/micilini/NessStudio/blob/main/native/libnessstudio/README.md) for application integration.
