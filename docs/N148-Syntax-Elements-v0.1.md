# N.148 Syntax Elements v0.1

## Objetivo

Listar de forma objetiva quais syntax elements existem no pipeline atual e quais já entram no subsistema CABAC nesta rodada.

---

## 1. Elementos já existentes no pipeline atual

### 1.1 Elementos por bloco

- `block_mode`
- `intra_mode`
- `ref_idx`
- `mvx`
- `mvy`
- `has_residual`
- `qp_delta`
- `coeff_count`
- `coeff_level[]`

### 1.2 Estado atual de transporte

No estado atual do código:
- alguns elementos seguem no caminho raw/bitstream principal
- alguns elementos já desviam para CABAC quando `entropy_mode == CABAC`

---

## 2. Mapeamento da v0.1

### 2.1 Permanecem fora do CABAC por enquanto

Esses elementos continuam no path principal do bitstream durante a ETAPA A:

- `block_mode`
- `intra_mode`
- `ref_idx`
- `has_residual`
- `qp_delta`

Motivo:
- manter a reorganização física sem quebrar o pipeline inteiro antes da hora
- reduzir o risco durante a FASE 7R.2

### 2.2 Entram no subsistema CABAC já nesta arquitetura

- `mvx`
- `mvy`
- `coeff_count`
- `coeff_level[]`

Motivo:
- são exatamente os pontos já exercitados pela API pública atual
- permitem atacar o problema de roundtrip e depois expandir cobertura

---

## 3. Ordem de encode/decode por tipo de bloco

### 3.1 Bloco intra sem residual

1. `block_mode`
2. `intra_mode`
3. `has_residual = 0`

### 3.2 Bloco intra com residual

1. `block_mode`
2. `intra_mode`
3. `has_residual = 1`
4. `qp_delta`
5. `coeff_count` via CABAC ou CAVLC
6. `coeff_level[]` via CABAC ou CAVLC

### 3.3 Bloco inter skip

1. `block_mode = skip`
2. sem residual
3. sem coeffs

### 3.4 Bloco inter com residual

1. `block_mode = inter`
2. `ref_idx`
3. `mvx`, `mvy` via CABAC ou CAVLC
4. `has_residual`
5. se residual:
   - `qp_delta`
   - `coeff_count`
   - `coeff_level[]`

---

## 4. Contextos por syntax element nesta v0.1

| Syntax element | Context ID | Binarização | Observação |
|---|---:|---|---|
| `mvx` | `N148_CTX_MV_X` | signed magnitude | componente horizontal |
| `mvy` | `N148_CTX_MV_Y` | signed magnitude | componente vertical |
| `coeff_count` | `N148_CTX_COEFF_CNT` | unary | quantidade de coeficientes válidos |
| `coeff_level[i]` | `N148_CTX_COEFF_SIG` + mag helper | signed magnitude | nível zig-zag atual |

---

## 5. Regras de compatibilidade durante a ETAPA A

Para não quebrar o projeto no meio da refatoração:

1. `n148_cabac.h` continua existindo como fachada pública
2. `n148_contexts.h` continua existindo como header de compatibilidade
3. `n148_binarization.h` continua existindo como header de compatibilidade
4. encoder e decoder continuam chamando:
   - `n148_cabac_session_init_enc/dec`
   - `n148_cabac_write_mv/read_mv`
   - `n148_cabac_write_block/read_block`
5. a implementação real passa a morar em módulos separados

---

## 6. Próxima expansão prevista

Depois da ETAPA A, a tendência natural é migrar também para o subsistema CABAC:
- `block_mode`
- `ref_idx`
- `has_residual`
- `qp_delta`
- flags de significância/last/runs mais refinadas

Mas isso só entra depois que engine + MV + residual básico estiverem estáveis.