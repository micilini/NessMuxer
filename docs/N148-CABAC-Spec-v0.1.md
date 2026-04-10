# N.148 CABAC Specification — v0.1

## Purpose

Define the first operational mini-spec for N.148 CABAC to enable consistent implementation between encoder and decoder.

This document does not attempt to copy H.264 bit-for-bit. It defines the internal rules for N.148 so that the CABAC subsystem has a stable contract.

---

## 1. Scope of this Version

This v0.1 covers:
- Reference CABAC engine in C
- Initial contexts per syntax class
- Binarization used by the first public syntax elements
- Per-slice lifecycle
- Encode/decode rules for 4x4 blocks and motion vectors

This v0.1 does not yet cover in final form:
- Fine compression tuning
- Advanced init table per QP
- Specialized contexts for all future blocks
- Architecture-specific optimizations

---

## 2. Architecture Principle

The N.148 CABAC stack is divided into five layers:

1. **Engine**
   - `low`
   - `range`
   - `code`
   - renormalization
   - bypass
   - termination

2. **Contexts**
   - initial state
   - initial MPS
   - state evolution

3. **Binarization**
   - unary
   - signed magnitude
   - extensible to truncated unary, fixed-length, and future mappings

4. **Syntax Layer**
   - MVD / public MV
   - coeff_count
   - coeff levels

5. **Slice Lifecycle**
   - per-slice init
   - sequential consumption
   - finish/terminate at slice end

---

## 3. Syntax Order within Slice

### 3.1 Macro Order for Each Block

For each N.148 4x4 block:

1. `block_mode` (outside CABAC v0.1 scope; follows current main bitstream path)
2. `intra_mode` or `ref_idx` (outside CABAC v0.1 scope; follows current main bitstream path)
3. `mvx`, `mvy` when block is inter and entropy = CABAC
4. `has_residual` (outside CABAC v0.1 scope; follows current main bitstream path)
5. `qp_delta` (outside CABAC v0.1 scope; currently remains in raw path)
6. `coeff_count` when there is residual and entropy = CABAC
7. `coeff_level[i]` for `i = 0 .. coeff_count - 1`

### 3.2 Motion Vector Order

CABAC motion vector is always written in:
- `mvx`
- `mvy`

Each component is independent.

---

## 4. Context Classes

### 4.1 Context IDs Used in this Version

- `N148_CTX_BLOCK_MODE`
- `N148_CTX_MV_X`
- `N148_CTX_MV_Y`
- `N148_CTX_QP_DELTA`
- `N148_CTX_COEFF_CNT`
- `N148_CTX_COEFF_SIG`
- `N148_CTX_COEFF_MAG`

### 4.2 Initial Policy v0.1

In v0.1, all contexts start in neutral state.

Initial state:
- `state = 32`
- `mps = 0` for sign/sig flags
- `mps = 1` for magnitudes/counts when current modeling requires 1-based terminator

### 4.3 Future Policy

In future versions, init may vary by:
- profile
- frame type
- base qp
- slice class

But this does not enter v0.1 implementation yet.

---

## 5. Binarization

### 5.1 Contextual Unary

Current use:
- `coeff_count`
- magnitude minus 1

Rule:
- write `value` zeros followed by a final bin `1`
- decoder reads until finding `1`

### 5.2 Contextual Signed Magnitude

Current use:
- `mvx`
- `mvy`
- `coeff_level`

Rule:
- significance bin
- sign bin
- magnitude minus 1 coded with contextual unary

Conventions:
- value `0` -> only `sig = 0`
- non-zero value -> `sig = 1`, then `sign`, then `mag-1`

---

## 6. Slice Lifecycle

### 6.1 CABAC Slice Start

When `entropy_mode == N148_ENTROPY_CABAC`:
- initialize `N148CabacSession`
- initialize `core`
- initialize `context set`

### 6.2 During Slice

- all syntax elements belonging to the CABAC path must be consumed in stable order
- "peek" reading of fields belonging to the CABAC stream itself is not permitted without explicit subsystem API
- engine state cannot be "restored" by raw manipulation of `byte_pos/bit_pos`

### 6.3 Slice End

- call `n148_cabac_session_finish_enc()` in encoder
- decoder naturally consumes what was written by encoder, without additional artificial flush

---

## 7. Operational Rules for this Phase

1. First the reference implementation in C
2. Then isolated tests per layer
3. Only then epic pipeline correction
4. Only then compression tuning
5. Assembly only after proven stability

---

## 8. Acceptance Criteria for this Spec

The v0.1 spec is considered sufficient when:
- encoder and decoder stop "guessing" element order
- engine, contexts, binarization, and syntax can be tested separately
- the team can implement PHASE 7R.3 without redefining the architecture