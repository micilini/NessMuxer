# N.148 CABAC Spec v0.1

## Objetivo

Definir a primeira mini-spec operacional do CABAC do N.148 para permitir implementação consistente entre encoder e decoder.

Este documento não tenta copiar H.264 bit a bit.
Ele define as regras internas do N.148 para que o subsistema CABAC tenha contrato estável.

---

## 1. Escopo desta versão

Esta v0.1 cobre:
- engine CABAC de referência em C
- contexts iniciais por classe de sintaxe
- binarização usada pelos primeiros syntax elements públicos
- ciclo de vida por slice
- regras de encode/decode para blocos 4x4 e motion vectors

Esta v0.1 ainda não cobre de forma final:
- tuning fino de compressão
- tabela avançada de init por QP
- contexts especializados para todos os blocos futuros
- otimizações por arquitetura

---

## 2. Princípio de arquitetura

A pilha CABAC do N.148 é dividida em cinco camadas:

1. **engine**
- `low`
- `range`
- `code`
- renormalização
- bypass
- término

2. **contexts**
- estado inicial
- MPS inicial
- evolução de estado

3. **binarização**
- unary
- signed magnitude
- extensível para truncated unary, fixed-length e mapeamentos futuros

4. **syntax layer**
- MVD / MV públicos
- coeff_count
- coeff levels

5. **slice lifecycle**
- init por slice
- consumo sequencial
- finish/terminate no final do slice

---

## 3. Ordem de sintaxe dentro do slice

### 3.1 Ordem macro de cada bloco

Para cada bloco 4x4 do N.148:

1. `block_mode` (fora do escopo CABAC v0.1; segue caminho atual do bitstream principal)
2. `intra_mode` ou `ref_idx` (fora do escopo CABAC v0.1; segue caminho atual do bitstream principal)
3. `mvx`, `mvy` quando bloco for inter e entropy = CABAC
4. `has_residual` (fora do escopo CABAC v0.1; segue caminho atual do bitstream principal)
5. `qp_delta` (fora do escopo CABAC v0.1; hoje permanece no path raw)
6. `coeff_count` quando houver residual e entropy = CABAC
7. `coeff_level[i]` para `i = 0 .. coeff_count - 1`

### 3.2 Ordem dos motion vectors

Motion vector CABAC é escrito sempre em:
- `mvx`
- `mvy`

Cada componente é independente.

---

## 4. Context classes

### 4.1 IDs de contexto usados nesta versão

- `N148_CTX_BLOCK_MODE`
- `N148_CTX_MV_X`
- `N148_CTX_MV_Y`
- `N148_CTX_QP_DELTA`
- `N148_CTX_COEFF_CNT`
- `N148_CTX_COEFF_SIG`
- `N148_CTX_COEFF_MAG`

### 4.2 Política inicial v0.1

Na v0.1, todos os contexts começam em estado neutro.

Estado inicial:
- `state = 32`
- `mps = 0` para sinais/sig flags
- `mps = 1` para magnitudes/contagens quando a modelagem atual exigir terminador baseado em 1

### 4.3 Política futura

Nas próximas versões, o init poderá variar por:
- profile
- frame type
- qp base
- slice class

Mas isso ainda não entra na v0.1 de implementação.

---

## 5. Binarização

### 5.1 Unary contextual

Uso atual:
- `coeff_count`
- magnitude menos 1

Regra:
- escrever `value` zeros seguidos de um bin final `1`
- o decoder lê até encontrar `1`

### 5.2 Signed magnitude contextual

Uso atual:
- `mvx`
- `mvy`
- `coeff_level`

Regra:
- bin de significância
- bin de sinal
- magnitude menos 1 codificada com unary contextual

Convenções:
- valor `0` -> apenas `sig = 0`
- valor não-zero -> `sig = 1`, depois `sign`, depois `mag-1`

---

## 6. Slice lifecycle

### 6.1 Início do slice CABAC

Quando `entropy_mode == N148_ENTROPY_CABAC`:
- inicializar `N148CabacSession`
- inicializar `core`
- inicializar `context set`

### 6.2 Durante o slice

- todos os syntax elements pertencentes ao path CABAC devem ser consumidos em ordem estável
- não é permitido fazer leitura “peek” de campos que pertençam ao próprio fluxo CABAC sem API explícita do subsistema
- o estado do engine não pode ser “restaurado” por manipulação crua de `byte_pos/bit_pos`

### 6.3 Final do slice

- chamar `n148_cabac_session_finish_enc()` no encoder
- o decoder consome o que foi escrito pelo encoder naturalmente, sem flush artificial adicional

---

## 7. Regras operacionais desta fase

1. Primeiro a implementação de referência em C
2. Depois testes isolados por camada
3. Só depois correção do pipeline épico
4. Só depois tuning de compressão
5. Assembly apenas após estabilidade comprovada

---

## 8. Critério de aceite desta spec

A spec v0.1 é considerada suficiente quando:
- encoder e decoder deixam de “adivinhar” a ordem dos elementos
- engine, contexts, binarização e sintaxe podem ser testados separadamente
- a equipe consegue implementar a FASE 7R.3 sem redefinir a arquitetura