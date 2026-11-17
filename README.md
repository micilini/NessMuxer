# NessMuxer

NessMuxer is a native encoding and Matroska library with public C interfaces. It provides H.264 encoder adapters, owned packets, explicit drain/finalize operations, bounded input queues and container writing. The implementation contains C and C++20; consumers can use C, C++ or another language through FFI.

The current versioned API is in [`include/ness_muxer_v2.h`](include/ness_muxer_v2.h). The original API in [`include/ness_muxer.h`](include/ness_muxer.h) remains available. NessStudio links this library statically inside libnessstudio; NessMuxer can also be built and used independently as a shared library.

## Backends and platform scope

| Capability | Windows x64 | Linux / macOS |
|---|---|---|
| Matroska/EBML, packet and codec ownership | Yes | Portable core |
| Direct Intel Quick Sync / oneVPL, D3D11 | Optional V2 backend | Not implemented |
| Direct NVIDIA NVENC, D3D11 | Optional V2 backend | Not implemented |
| Direct AMD AMF, D3D11 | Optional V2 backend | Not implemented |
| x264 software encoding | Pinned V2 runtime in the Windows preset | Requires a compatible external runtime/build configuration; not enabled in the portable preset |
| Platform legacy encoders | Optional Media Foundation | macOS VideoToolbox; Linux optional x264 |
| Experimental N.148 codec and utilities | Optional | Optional |

The Windows preset enables the direct GPU backends and x264 and disables Media Foundation. The portable preset exercises the container, ownership and N.148 components; it does not imply parity with the Windows GPU path. A compiled adapter still needs supported hardware and a working driver.

V2 selection is explicit: the caller requests a backend and handles `NESS_V2_E_UNAVAILABLE`. Automatic policy and fallback across those backends belong to the host, such as libnessstudio. V2 never silently replaces a requested GPU encoder with CPU encoding.

## Build on Windows

Install Git, CMake 3.28+ and Visual Studio 2026 with the v145 C++ x64 toolset. Run in a developer shell with CMake available:

```powershell
git clone https://github.com/micilini/NessMuxer.git
cd NessMuxer
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel 3
ctest --preset windows-x64 --no-tests=error
cmake --install .out/windows-x64 --config Release --prefix .out/package
```

The package contains `bin/NessMuxer.dll`, runtime DLLs, `lib/NessMuxer.lib`, public headers under `include/`, and notices/source material under `share/NessMuxer`. Put the DLLs beside the executable that loads NessMuxer. Keep the complete package's redistribution notices. Do not mix x86/x64 or Debug/Release binaries.

The bundled x264 headers/runtime, oneVPL source archive and GPU SDK headers are checked against `native-backends.lock.json`. Configure requires no download for these dependencies. Hardware drivers are supplied by the operating system/vendor, not by this repository.

## Build the portable configuration

Install a C11/C++20 compiler, CMake 3.28+ and Ninja. macOS additionally requires Xcode Command Line Tools.

```sh
cmake --preset portable
cmake --build --preset portable --parallel 3
ctest --preset portable --no-tests=error
cmake --install .out/portable --prefix .out/package
```

The installed library is `libNessMuxer.so` on Linux or `libNessMuxer.dylib` on macOS. For embedding, set `NESS_BUILD_SHARED=OFF` and `NESS_BUILD_TESTS=OFF`; link the `NessMuxer` CMake target so its compile definitions and link dependencies propagate. An external x264 installation and matching ABI headers are required if enabling that backend on a non-Windows build.

## Use the V2 API

1. Initialize all envelopes with the exact `sizeof` and `version = NESS_V2_VERSION`; zero reserved fields. Choose an explicit backend, dimensions, frame rate, bitrate and bounded pending-frame budget.
2. Create an encoder through `ness_encoder_create_v2` for CPU input or `ness_encoder_create_gpu_v2` with a compatible D3D11 device for GPU input. Report errors with their stage and message.
3. Submit NV12 CPU planes with `ness_encoder_submit_cpu_v2`. Input is copied before the call returns successfully. GPU submission returns an input lease: observe completion before reusing the surface and release the lease afterward.
4. Retrieve owned packets with `ness_encoder_receive_v2`. `NESS_V2_AGAIN` means no packet is ready yet; `NESS_V2_E_BACKPRESSURE` requires the caller to consume pending output before retrying input.
5. Obtain the codec description, open a writer with `ness_mkv_open_v2`, and write packets through `ness_mkv_write_packet_v2`. Packet/codec views are borrowed only while their owned handles remain alive.
6. Complete with `ness_encoder_drain_v2`, receive until `NESS_V2_END`, then call `ness_mkv_finalize_v2`. Release all handles. Discard/release do not count as successful finalization.

Encoder operations are bound to their creating thread. Timestamps and durations use 100-nanosecond units. Preserve frame IDs, DTS/PTS and ownership contracts rather than passing transient buffers across threads. Error results do not transfer ownership unless the public API says otherwise.

[`test/v2/x264.cpp`](test/v2/x264.cpp) and the other `test/v2` consumers demonstrate the complete encode/receive/drain/write lifecycle. The portable core tests cover bounded queues, packets, clocks and muxing boundaries. V2's Matroska writer currently handles a video track; audio capture/mixing and a complete recording application belong to the host.

## Automated builds and tests

The [GitHub Actions workflow](.github/workflows/build-libraries.yml) builds Windows x64, Linux x64 and macOS ARM64 packages, runs their configured tests and uploads install directories as downloadable artifacts. Actions are pinned to immutable revisions.

The Windows test preset excludes tests labeled `hardware`, because hosted runners cannot certify physical encoder availability. On a compatible Windows machine, run all tests with:

```powershell
ctest --test-dir .out/windows-x64 -C Release --output-on-failure
```

Driver-contract tests exercise API negotiation without claiming that every GPU model was tested. Real GPU tests still require the corresponding device and driver.

## Source layout

- `include/`: public C headers.
- `src/v2/`: versioned API, owned packets, writer and direct encoder adapters.
- `src/encoder/`: platform encoders; `src/codec/n148/` and `src/decoder/n148/`: experimental codec components.
- `cli/`: command-line consumers and codec inspection utilities, built with the test/tool option.
- `test/`: contract, container, encoder and codec tests.
- `cmake/`, `CMakePresets.json`: dependency resolution and repeatable builds.
- `third_party/`: pinned upstream headers/runtimes/source archives and required licenses.
- `ffmpeg-n148/`: source for the optional experimental FFmpeg codec integration; it is not part of the standard library build.

N.148 is experimental and has its own bitstream/container behavior; it is not an H.264 alias. The Windows native recording path uses the selected H.264 backend.

## License

NessMuxer's own code is [MIT](LICENSE). Optional dependencies retain their own terms: x264 is GPL, and Intel/NVIDIA/AMD headers and runtime components retain their notices. Windows packages built with the supplied preset include the x264 license and source archive. Do not interpret the repository's MIT license as relicensing these dependencies.
