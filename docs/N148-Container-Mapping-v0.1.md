# N.148 Container Mapping — Matroska (MKV) — v0.1

**Status:** Draft
**Data:** 2026-04

---

## 1. Visão Geral

O codec N.148 é encapsulado dentro do container Matroska (.mkv) usando
o mapeamento definido neste documento.

---

## 2. Track Entry

### 2.1 CodecID
V_NESS/N148

### 2.2 CodecPrivate

O campo `CodecPrivate` do TrackEntry contém o **Sequence Header** do N.148
serializado em formato binário (32 bytes, conforme a Bitstream Spec seção 3).

### 2.3 TrackType
TrackType = 1 (video)

### 2.4 FlagLacing
FlagLacing = 0 (desabilitado)

---

## 3. SimpleBlock Format

Cada `SimpleBlock` do Matroska contém um frame N.148 completo.

### 3.1 Empacotamento

Dentro do SimpleBlock, os NAL units do N.148 são encapsulados com
**length prefix de 4 bytes** (big-endian), sem start codes:
[nal_length: 4 bytes BE] [nal_data: N bytes]
[nal_length: 4 bytes BE] [nal_data: N bytes]
...

Ou seja, o mesmo formato "mp4-style" usado pelo AVC dentro do MKV,
mas com NALs do N.148.

### 3.2 Keyframe Flag

O bit de keyframe do SimpleBlock header é definido como `1` quando o frame
é do tipo I (IDR). Frames P e B têm keyframe = 0.

### 3.3 Timestamps

Os timestamps do MKV são em milissegundos (conforme TimestampScale = 1000000).
O muxer converte de HNS (100-nanosecond intervals) para ms ao escrever.

---

## 4. Cues

Cue points são gerados para cada keyframe (I-frame), apontando para o
início do cluster que contém o frame.

---

## 5. Exemplo de Estrutura
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

---

## 6. Compatibilidade

- **mkvinfo**: mostrará o codec como desconhecido, mas a estrutura EBML
  será válida e navegável.
- **ffprobe**: reportará codec desconhecido `V_NESS/N148`.
- **Players**: não reproduzirão sem suporte ao codec N.148 (esperado).