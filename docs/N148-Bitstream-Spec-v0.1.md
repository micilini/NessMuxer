# N.148 Bitstream Specification — v0.1

**Status:** Draft — sujeito a revisão até Fase 3 concluída
**Autor:** NessMuxer Project
**Data:** 2026-04

---

## 1. Visão Geral

O N.148 é um codec de vídeo autoral com bitstream próprio, não compatível com
H.264/AVC, H.265/HEVC ou qualquer padrão ISO/ITU existente.

O codec opera sobre vídeo planar YCbCr 4:2:0, com profundidade de 8 bits
(extensível para 10 bits em versões futuras).

### 1.1 Profiles

| Profile ID | Nome  | Frame Types | Entropy | Motion       |
|------------|-------|-------------|---------|--------------|
| 0x01       | Main  | I + P       | CAVLC   | Full-pel ME  |
| 0x02       | Epic  | I + P + B   | CABAC   | Sub-pel ME   |

### 1.2 Byte Order

Todos os campos multi-byte são codificados em **big-endian** (network byte order).

---

## 2. NAL Unit Structure

Cada NAL unit do N.148 segue o formato:[start_code: 3 bytes]  0x00 0x00 0x01
[nal_header: 1 byte]
[payload:    N bytes]

### 2.1 NAL Header (1 byte)bit 7    : forbidden_zero_bit (deve ser 0)
bits 6-4 : nal_type (0..7)
bits 3-0 : reserved (devem ser 0)

### 2.2 NAL Types

| nal_type | Valor | Descrição             |
|----------|-------|-----------------------|
| SLICE    | 0     | Slice de frame normal (P-frame) |
| IDR      | 1     | Slice IDR (I-frame)   |
| SEQ_HDR  | 2     | Sequence Header       |
| FRM_HDR  | 3     | Frame Header          |
| SEI      | 4     | Supplemental info     |
| 5-7      | —     | Reservado             |

### 2.3 Emulation Prevention

Dentro do payload de cada NAL, a sequência `0x00 0x00 0x00`, `0x00 0x00 0x01`
e `0x00 0x00 0x02` deve ser evitada. Para isso, insere-se um byte de emulation
prevention `0x03` após `0x00 0x00`:0x00 0x00 0x00  →  0x00 0x00 0x03 0x00
0x00 0x00 0x01  →  0x00 0x00 0x03 0x01
0x00 0x00 0x02  →  0x00 0x00 0x03 0x02
0x00 0x00 0x03  →  0x00 0x00 0x03 0x03

---

## 3. Sequence Header (NAL type 2)

O Sequence Header descreve os parâmetros globais do stream. É transmitido
antes do primeiro frame e deve preceder qualquer IDR.

### 3.1 Layout Binário

| Offset | Size    | Campo                | Descrição                                |
|--------|---------|----------------------|------------------------------------------|
| 0      | 4 bytes | magic                | `0x4E 0x31 0x34 0x38` ("N148")           |
| 4      | 1 byte  | version              | Versão da spec (1 para v0.1)             |
| 5      | 1 byte  | profile              | 0x01=Main, 0x02=Epic                     |
| 6      | 1 byte  | level                | Nível (1-10, define limites de resolução)|
| 7      | 1 byte  | chroma_format        | 0x01=4:2:0                               |
| 8      | 1 byte  | bit_depth            | 8 ou 10                                  |
| 9      | 2 bytes | width                | Largura em pixels (BE)                   |
| 11     | 2 bytes | height               | Altura em pixels (BE)                    |
| 13     | 4 bytes | timescale            | Unidades de tempo por segundo (BE)       |
| 17     | 2 bytes | fps_num              | Frame rate numerador (BE)                |
| 19     | 2 bytes | fps_den              | Frame rate denominador (BE)              |
| 21     | 2 bytes | gop_length           | Tamanho do GOP default (BE)              |
| 23     | 1 byte  | entropy_mode         | 0x01=CAVLC, 0x02=CABAC                  |
| 24     | 1 byte  | max_ref_frames       | Máximo de reference frames               |
| 25     | 1 byte  | max_reorder_depth    | 0=Main, >0=Epic (para B-frames)          |
| 26     | 1 byte  | block_size_flags     | Bitmask: bit0=4x4, bit1=8x8, bit2=16x16 |
| 27     | 2 bytes | feature_flags        | Bitmask de features (reservado)          |
| 29     | 3 bytes | reserved             | Padding para extensibilidade (0x00)      |

**Tamanho total: 32 bytes fixos.**

### 3.2 Block Size Flags (byte 26)bit 0 : suporta blocos 4x4
bit 1 : suporta blocos 8x8
bit 2 : suporta blocos 16x16
bit 3 : suporta blocos 32x32 (futuro)
bits 4-7 : reservado

Para Main profile v0.1, o valor padrão é `0x07` (4x4 + 8x8 + 16x16).

---

## 4. Frame Header (NAL type 3)

Cada frame começa com um Frame Header NAL, seguido de um ou mais Slice NALs.

### 4.1 Layout Binário

| Offset | Size    | Campo              | Descrição                        |
|--------|---------|--------------------|----------------------------------|
| 0      | 1 byte  | frame_type         | 0x01=I, 0x02=P, 0x03=B           |
| 1      | 4 bytes | frame_number       | Número sequencial do frame (BE)   |
| 5      | 8 bytes | pts                | Presentation timestamp (BE, HNS)  |
| 13     | 8 bytes | dts                | Decode timestamp (BE, HNS)        |
| 21     | 1 byte  | qp_base            | QP base para o frame (0-51)       |
| 22     | 2 bytes | slice_count        | Número de slices no frame (BE)    |
| 24     | 1 byte  | num_ref_frames     | Refs usadas neste frame            |
| 25     | N bytes | ref_frame_indices  | Lista de índices (1 byte cada)     |
| 25+N   | 4 bytes | frame_data_size    | Tamanho dos dados do frame (BE)    |

**Tamanho: 29 + num_ref_frames bytes.**

Para I-frames, `num_ref_frames = 0`.

---

## 5. Slice Data (NAL type 0 ou 1)

Cada slice contém uma sequência de blocos codificados.

### 5.1 Slice Header

| Offset | Size    | Campo            | Descrição                       |
|--------|---------|------------------|---------------------------------|
| 0      | 2 bytes | first_mb_index   | Índice do primeiro macrobloco (BE)|
| 2      | 2 bytes | mb_count         | Número de macroblocos no slice (BE)|

### 5.2 Macroblock (16×16)

Cada macrobloco de 16×16 pixels é composto por:
- 16 blocos de luma 4×4 (ou 4 blocos de 8×8)
- 4 blocos de chroma-U 4×4 (2×2 no espaço 4:2:0)
- 4 blocos de chroma-V 4×4

### 5.3 Block Syntax

Para cada bloco, o bitstream contém (codificado via CAVLC/Exp-Golomb):
block_type          : ue(v) — 0=intra, 1=inter_skip, 2=inter_single
Se intra:
a. intra_mode       : ue(v) — modo de predição intra
b. has_residual     : u(1)
c. Se has_residual:
i. cbp           : ue(v) — coded block pattern
ii. qp_delta     : se(v)
iii. coefficients: codificados via CAVLC run-level
Se inter (futuro, Fases 5+):
a. mvd_x            : se(v)
b. mvd_y            : se(v)
c. has_residual     : u(1)
d. (residual igual ao intra)


### 5.4 Intra Prediction Modes

| Mode ID | Nome         | Descrição                                |
|---------|--------------|------------------------------------------|
| 0       | DC           | Média dos vizinhos acima e à esquerda    |
| 1       | Horizontal   | Propaga vizinhos à esquerda              |
| 2       | Vertical     | Propaga vizinhos acima                   |
| 3       | Diag Down-Left  | Diagonal para baixo-esquerda          |
| 4       | Diag Down-Right | Diagonal para baixo-direita           |
| 5       | Planar       | Interpolação bilinear                    |

### 5.5 CAVLC Residual Coding

Os coeficientes transformados e quantizados são codificados com:

1. **coeff_token**: Exp-Golomb unsigned — número total de coeficientes
   não-zero e trailing ones
2. **sign flags**: 1 bit por trailing one
3. **levels**: Exp-Golomb signed para cada coeficiente restante
4. **total_zeros**: Exp-Golomb unsigned — total de zeros antes do último
   coeficiente
5. **run_before**: Exp-Golomb unsigned — quantidade de zeros antes de cada
   coeficiente (do último para o primeiro)

---

## 6. Transform

### 6.1 Forward DCT 4×4

A transformada usa a DCT inteira (butterfly-based), sem multiplicação de ponto
flutuante, similar ao H.264 mas com matriz própria.

Matriz de transformada N.148 4×4: | 1   1   1   1 |
T =  | 2   1  -1  -2 |
| 1  -1  -1   1 |
| 1  -2   2  -1 |

`Y = T · X · T^T`

### 6.2 Forward DCT 8×8

Extensão da DCT 4×4 para blocos maiores. Usada quando block_size_flags
indica suporte a 8×8.

---

## 7. Quantização

### 7.1 QP Range

QP varia de 0 (menor quantização, maior qualidade) a 51 (maior quantização).

### 7.2 Qstep

`Qstep = pow(2.0, (QP - 4) / 6.0)`

### 7.3 Dead-zone Quantizationlevel = sign(coeff) * max(0, (abs(coeff) * quant_scale + offset) >> shift)

Onde `offset` implementa a dead-zone: coeficientes próximos de zero são
arredondados para zero mais agressivamente.

### 7.4 Zig-Zag Scan Order (4×4)0  1  5  6
2  4  7 12
3  8 11 13
9 10 14 15

---

## 8. Container Mapping (MKV)

Ver documento separado: `N148-Container-Mapping-v0.1.md`.

---

## 9. Versionamento

- **v0.1**: I-frame only, Main profile, CAVLC, 4:2:0, 8-bit
- **v0.2** (planejado): P-frames, motion estimation
- **v0.3** (planejado): B-frames, Epic profile, CABAC