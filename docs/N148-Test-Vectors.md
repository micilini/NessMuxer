# N.148 Test Vectors — v0.1

**Status:** Draft
**Date:** 2026-04

---

## 1. Test Vector Format

Each test vector consists of:

| File              | Description                                  |
|-------------------|----------------------------------------------|
| `<n>.nv12`        | Input frames in raw NV12 format              |
| `<n>.n148`        | Raw N.148 bitstream (NALs with start codes)  |
| `<n>.mkv`         | Bitstream muxed in Matroska                  |
| `<n>.decoded.nv12`| Decoded output for comparison                |

---

## 2. Planned Vectors

### 2.1 TV-001: Flat Gray (Intra-Only)

- **Resolution:** 64×48
- **Frames:** 1
- **Profile:** Main
- **Content:** Entire frame with Y=128, U=128, V=128
- **Purpose:** Validate pure DC prediction, simplest possible case
- **Criteria:** decoded == original (bit-exact)

### 2.2 TV-002: Color Bars (Intra-Only)

- **Resolution:** 128×96
- **Frames:** 1
- **Profile:** Main
- **Content:** Vertical color bars (8 bars: white, yellow, cyan, green, magenta, red, blue, black)
- **Purpose:** Validate intra modes with sharp edges
- **Criteria:** PSNR ≥ 40dB (with QP=10)

### 2.3 TV-003: Gradient (Intra-Only)

- **Resolution:** 320×240
- **Frames:** 1
- **Profile:** Main
- **Content:** Diagonal gradient Y=f(x,y), constant UV
- **Purpose:** Validate planar and horizontal/vertical prediction
- **Criteria:** PSNR ≥ 35dB (with QP=20)

### 2.4 TV-004: Multi-Frame Intra (Intra-Only)

- **Resolution:** 160×120
- **Frames:** 30
- **Profile:** Main
- **Content:** Synthetic NV12 pattern varying per frame
- **Purpose:** Validate GOP (keyframe interval), multiple frames, timestamps
- **Criteria:** All 30 frames decode without error

### 2.5 TV-005: Static P-Frame (Future — Phase 5)

- **Resolution:** 320×240
- **Frames:** 30
- **Profile:** Main
- **Content:** Static image repeated 30×
- **Purpose:** P-frames with skip mode (no motion, no residual)

### 2.6 TV-006: Motion P-Frame (Future — Phase 5)

- **Resolution:** 320×240
- **Frames:** 60
- **Profile:** Main
- **Content:** Rectangle moving horizontally

---

## 3. How to Generate

```bash
# Generate flat gray NV12 (64x48, 1 frame)
# Y plane: 64*48 = 3072 bytes of 0x80
# UV plane: 64*24 = 1536 bytes of 0x80
python3 -c "
import sys
w, h = 64, 48
y = bytes([128] * (w * h))
uv = bytes([128] * (w * h // 2))
sys.stdout.buffer.write(y + uv)
" > tv001_flat_gray.nv12

# Encode
nessmux tv001_flat_gray.nv12 tv001_flat_gray.mkv \
  --width 64 --height 48 --fps 30 --bitrate 500 --codec n148

# Decode
n148dec tv001_flat_gray.mkv tv001_flat_gray.decoded.nv12

# Compare
cmp tv001_flat_gray.nv12 tv001_flat_gray.decoded.nv12
```

---

## 4. Reference Hashes

To be filled when encoder and decoder are fully functional.

| Vector  | Input SHA256 | Bitstream SHA256 | Decoded SHA256 | Match? |
|---------|--------------|------------------|----------------|--------|
| TV-001  | (pending)    | (pending)        | (pending)      | —      |
| TV-002  | (pending)    | (pending)        | (pending)      | —      |
| TV-003  | (pending)    | (pending)        | (pending)      | —      |
| TV-004  | (pending)    | (pending)        | (pending)      | —      |