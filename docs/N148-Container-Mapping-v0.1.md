# N.148 Container Mapping — Matroska (MKV) — v0.1

**Status:** Draft
**Date:** 2026-04

---

## 1. Overview

The N.148 codec is encapsulated within the Matroska (.mkv) container using the mapping defined in this document.

---

## 2. Track Entry

### 2.1 CodecID

```
V_NESS/N148
```

### 2.2 CodecPrivate

The `CodecPrivate` field of the TrackEntry contains the **Sequence Header** of N.148 serialized in binary format (32 bytes, as per Bitstream Spec section 3).

### 2.3 TrackType

```
TrackType = 1 (video)
```

### 2.4 FlagLacing

```
FlagLacing = 0 (disabled)
```

---

## 3. SimpleBlock Format

Each Matroska `SimpleBlock` contains one complete N.148 frame.

### 3.1 Packaging

Inside the SimpleBlock, N.148 NAL units are encapsulated with a **4-byte length prefix** (big-endian), without start codes:

```
[nal_length: 4 bytes BE] [nal_data: N bytes]
[nal_length: 4 bytes BE] [nal_data: N bytes]
...
```

This is the same "mp4-style" format used by AVC inside MKV, but with N.148 NALs.

### 3.2 Keyframe Flag

The keyframe bit in the SimpleBlock header is set to `1` when the frame is type I (IDR). P and B frames have keyframe = 0.

### 3.3 Timestamps

MKV timestamps are in milliseconds (as per TimestampScale = 1000000). The muxer converts from HNS (100-nanosecond intervals) to ms when writing.

---

## 4. Cues

Cue points are generated for each keyframe (I-frame), pointing to the start of the cluster containing the frame.

---

## 5. Structure Example

```
EBML Header
Segment
├── SeekHead
├── Info (TimestampScale=1000000, Duration=...)
├── Tracks
│   └── TrackEntry
│       ├── TrackNumber: 1
│       ├── TrackType: video
│       ├── CodecID: "V_NESS/N148"
│       ├── CodecPrivate: [32 bytes seq_header]
│       ├── FlagLacing: 0
│       └── Video
│           ├── PixelWidth: 1920
│           └── PixelHeight: 1080
├── Cluster (Timestamp: 0)
│   ├── SimpleBlock [keyframe] [frame 0 NALs length-prefixed]
│   ├── SimpleBlock [frame 1 NALs]
│   └── ...
├── Cluster (Timestamp: 1000)
│   └── ...
└── Cues
    ├── CuePoint (Time=0, Track=1, ClusterPosition=...)
    └── CuePoint (Time=1000, ...)
```

---

## 6. Compatibility

- **mkvinfo**: Will show the codec as unknown, but the EBML structure will be valid and navigable.
- **ffprobe**: Will report unknown codec `V_NESS/N148`.
- **Players**: Will not play without N.148 codec support (expected).