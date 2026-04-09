# N.148 — Estado atual da FASE 7 antes da refatoração 7R

## Objetivo deste documento

Congelar o estado atual da FASE 7 antes de qualquer nova tentativa de “corrigir por cima”.
A partir deste ponto, o CABAC do N.148 deixa de ser tratado como ajuste incremental e passa a ser tratado como subsistema dedicado.

## Sintomas confirmados no estado atual

### 1. Roundtrip público de MV falhando

Teste:
- `test_n148_cabac`

Sintoma observado:
- `CABAC public MV roundtrip` falha com `mv mismatch`

Conclusão:
- O pipeline encode/decode CABAC ainda não preserva bit-exatidão nem para um syntax element simples e isolado.

### 2. Decoder épico falhando na inicialização/consumo do slice CABAC

Teste:
- `test_n148_phase7_epic`

Sintoma observado:
- caso `EPIC/CABAC` falha com `rc=114`

Observação:
- o caminho `MAIN/CAVLC` permanece funcional.

Conclusão:
- o problema está concentrado no stack CABAC e não no pipeline geral de codec/mux/reorder.

### 3. Tamanho CABAC anômalo frente ao CAVLC

Mesmo com o estado atual ainda não estar em conformidade, o comportamento observado indica:
- `MAIN/CAVLC`: ~4270 bytes no teste épico
- `EPIC/CABAC`: ~12262 bytes no teste épico

Conclusão:
- há forte indício de desalinhamento entre engine, binarização e modelagem de contextos.
- o CABAC atual não está só incorreto: ele também está ineficiente.

## Diagnóstico arquitetural

Hoje o CABAC está concentrado principalmente em `src/common/entropy/n148_cabac.c`, contendo ao mesmo tempo:
- engine aritmético
- helper de binarização
- contexto simplificado
- sessão por-slice
- syntax helpers públicos (`write_mv`, `read_mv`, `write_block`, `read_block`)

Isso gera três problemas:

1. Acoplamento excessivo
- engine, contextos e sintaxe evoluem no mesmo arquivo.

2. Testabilidade ruim
- fica difícil validar isoladamente engine, binarização, contextos e layer de sintaxe.

3. Integração frágil
- encoder e decoder acabam enxergando CABAC como função utilitária, e não como subsistema.

## Decisão oficial

A FASE 7 antiga é considerada encerrada como experimento.

O caminho aprovado passa a ser:
- FASE 7R.0 — congelar escopo e registrar estado
- FASE 7R.1 — formalizar a mini-spec CABAC do N.148
- FASE 7R.2 — reorganizar fisicamente o código
- FASE 7R.3 em diante — corrigir engine, contextos, binarização e sintaxe em camadas separadas

## O que NÃO fazer a partir daqui

- Não continuar adicionando remendos em `n148_cabac.c`
- Não expandir o CABAC atual “do jeito que está”
- Não tentar otimização/assembly antes de passar na implementação de referência em C

## O que passa a valer

1. Correção bit-exata antes de compressão
2. Conformidade de sintaxe antes de performance
3. Subsistema dedicado antes de otimização por arquitetura