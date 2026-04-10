# PHASE 10.3 — Windows 11 / ffplay / VLC

## Operational truth

The `.mkv` file with `CodecID = V_NESS/N148` will not be opened by the VLC already installed on the system just by copying NessMuxer DLLs.

For an external player to recognize the codec, it is necessary to have:

1. a fork of FFmpeg with:
   - `AV_CODEC_ID_N148`
   - registered N.148 parser
   - registered N.148 decoder
   - Matroska mapping `V_NESS/N148 -> AV_CODEC_ID_N148`

2. build a player that uses this custom FFmpeg:
   - first target: `ffplay`
   - second optional target: custom VLC

## Goal of phase 10.3

### Minimum victory
Open `test_n148_mux.mkv` and `test_n148_mux_cabac.mkv` in the custom `ffplay.exe`.

### Advanced victory
Open the same files in a custom VLC built against the updated stack.

---

## Recommended order

### Step 1
Validate that NessMuxer continues generating:

- `test_n148_mux.mkv`
- `test_n148_mux_cabac.mkv`

### Step 2
Apply the patches from the `ffmpeg-n148/patches/` directory to the FFmpeg fork.

### Step 3
Build the custom `ffplay.exe`.

### Step 4
Test:

```powershell
.\ffplay.exe .\test_n148_mux.mkv
.\ffplay.exe .\test_n148_mux_cabac.mkv
```
