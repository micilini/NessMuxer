# N.148 Test Vectors — v0.1

**Status:** Draft
**Data:** 2026-04

---

## 1. Formato dos Test Vectors

Cada test vector consiste em:

| Arquivo              | Descrição                                    |
|----------------------|----------------------------------------------|
| `<name>.nv12`        | Frames de entrada em formato NV12 raw        |
| `<name>.n148`        | Bitstream N.148 raw (NALs com start codes)   |
| `<name>.mkv`         | Bitstream muxado em Matroska                 |
| `<name>.decoded.nv12`| Saída decodificada para comparação           |

---

## 2. Vetores Planejados

### 2.1 TV-001: Flat Gray (Intra-Only)

- **Resolução:** 64×48
- **Frames:** 1
- **Profile:** Main
- **Conteúdo:** Frame inteiro com Y=128, U=128, V=128
- **Propósito:** Validar DC prediction puro, caso mais simples possível
- **Critério:** decoded == original (bit-exato)

### 2.2 TV-002: Color Bars (Intra-Only)

- **Resolução:** 128×96
- **Frames:** 1
- **Profile:** Main
- **Conteúdo:** Barras de cor verticais (8 barras: branco, amarelo, ciano,
  verde, magenta, vermelho, azul, preto)
- **Propósito:** Validar modos intra com bordas nítidas
- **Critério:** PSNR ≥ 40dB (com QP=10)

### 2.3 TV-003: Gradient (Intra-Only)

- **Resolução:** 320×240
- **Frames:** 1
- **Profile:** Main
- **Conteúdo:** Gradiente diagonal Y=f(x,y), UV constante
- **Propósito:** Validar predição planar e horizontal/vertical
- **Critério:** PSNR ≥ 35dB (com QP=20)

### 2.4 TV-004: Multi-Frame Intra (Intra-Only)

- **Resolução:** 160×120
- **Frames:** 30
- **Profile:** Main
- **Conteúdo:** Padrão NV12 sintético variando por frame
- **Propósito:** Validar GOP (keyframe interval), múltiplos frames, timestamps
- **Critério:** Todos os 30 frames decodificam sem erro

### 2.5 TV-005: Static P-Frame (Futuro — Fase 5)

- **Resolução:** 320×240
- **Frames:** 30
- **Profile:** Main
- **Conteúdo:** Imagem estática repetida 30×
- **Propósito:** P-frames com skip mode (sem movimento, sem residual)

### 2.6 TV-006: Motion P-Frame (Futuro — Fase 5)

- **Resolução:** 320×240
- **Frames:** 60
- **Profile:** Main
- **Conteúdo:** Retângulo se movendo horizontalmente

---

## 3. Como Gerar
```bash
# Gerar flat gray NV12 (64x48, 1 frame)
# Y plane: 64*48 = 3072 bytes de 0x80
# UV plane: 64*24 = 1536 bytes de 0x80
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

## 4. Hashes de Referência

Serão preenchidos quando o encoder e decoder estiverem funcionais.

| Vector  | Input SHA256 | Bitstream SHA256 | Decoded SHA256 | Match? |
|---------|--------------|------------------|----------------|--------|
| TV-001  | (pendente)   | (pendente)       | (pendente)     | —      |
| TV-002  | (pendente)   | (pendente)       | (pendente)     | —      |
| TV-003  | (pendente)   | (pendente)       | (pendente)     | —      |
| TV-004  | (pendente)   | (pendente)       | (pendente)     | —      |