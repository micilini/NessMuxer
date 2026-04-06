#!/bin/bash
# =========================================================================
# NessMuxer - Build & Test Script (Linux)
# =========================================================================
#
# Uso:
#   ./build_and_test.sh              (build com x264 - padrao no Linux)
#   ./build_and_test.sh --no-x264    (build sem nenhum encoder - so testa muxer/ebml/avc)
#   ./build_and_test.sh --raw <path> (especifica arquivo .raw para testes CLI)
#
# Requisitos:
#   sudo apt install build-essential cmake pkg-config libx264-dev
#
# =========================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="$SCRIPT_DIR/build"
USE_X264="ON"
RAW_FILE=""
WIDTH=1920
HEIGHT=1080
FPS=30
BITRATE=6000

# =========================================================================
# Parse argumentos
# =========================================================================
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-x264)
            USE_X264="OFF"
            shift
            ;;
        --raw)
            RAW_FILE="$2"
            shift 2
            ;;
        --width)
            WIDTH="$2"
            shift 2
            ;;
        --height)
            HEIGHT="$2"
            shift 2
            ;;
        --fps)
            FPS="$2"
            shift 2
            ;;
        --bitrate)
            BITRATE="$2"
            shift 2
            ;;
        -h|--help)
            echo "Uso: ./build_and_test.sh [--no-x264] [--raw <path>] [--width W] [--height H] [--fps F] [--bitrate B]"
            exit 0
            ;;
        *)
            echo "Argumento desconhecido: $1"
            exit 1
            ;;
    esac
done

echo ""
echo "==================================================================="
echo " NessMuxer - Build & Test Script (Linux)"
echo "==================================================================="
echo " X264:     $USE_X264"
echo " Raw file: ${RAW_FILE:-(nenhum)}"
if [ -n "$RAW_FILE" ]; then
    echo " Resolucao: ${WIDTH}x${HEIGHT} @ ${FPS}fps, ${BITRATE}kbps"
fi
echo "==================================================================="
echo ""

# =========================================================================
# STEP 0 — Verificar dependencias
# =========================================================================
echo "[0/5] Verificando dependencias..."
echo ""

MISSING=""

if ! command -v cmake &>/dev/null; then
    MISSING="$MISSING cmake"
fi

if ! command -v gcc &>/dev/null && ! command -v cc &>/dev/null; then
    MISSING="$MISSING build-essential"
fi

if [ "$USE_X264" = "ON" ]; then
    if ! pkg-config --exists x264 2>/dev/null; then
        MISSING="$MISSING libx264-dev"
    fi
fi

if [ -n "$MISSING" ]; then
    echo "[FAIL] Dependencias faltando:$MISSING"
    echo ""
    echo "  Instale com:"
    echo "    sudo apt update"
    echo "    sudo apt install build-essential cmake pkg-config libx264-dev"
    echo ""
    exit 1
fi

echo "[OK] Todas as dependencias encontradas."
if [ "$USE_X264" = "ON" ]; then
    X264_VERSION=$(pkg-config --modversion x264 2>/dev/null || echo "?")
    echo "     x264 version: $X264_VERSION"
fi
echo ""

# =========================================================================
# STEP 1 — CMake Configure
# =========================================================================
echo "[1/5] CMake Configure..."
echo ""

cmake -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DNESS_USE_X264="$USE_X264" \
      "$SCRIPT_DIR"
echo ""
echo "[OK] CMake configure done."
echo ""

# =========================================================================
# STEP 2 — Build
# =========================================================================
echo "[2/5] Building..."
echo ""

cmake --build "$BUILD_DIR" --config Release -j$(nproc)
echo ""
echo "[OK] Build succeeded."
echo ""

# Verificar binarios
echo "Checking binaries:"
for bin in libNessMuxer.so test_ebml test_avc test_muxer_only test_full_pipeline nessmux nessmux_validate nessmux_bench; do
    if [ -f "$BUILD_DIR/$bin" ]; then
        echo "  [OK] $bin"
    else
        echo "  [MISSING] $bin"
    fi
done
echo ""

# =========================================================================
# STEP 3 — Testes unitarios
# =========================================================================
echo "[3/5] Running unit tests..."
echo ""

PASS_COUNT=0
FAIL_COUNT=0

# Garantir que o .so esta acessivel para os testes
export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"

run_test() {
    local name="$1"
    local bin="$BUILD_DIR/$2"

    echo "--- $name ---"
    if [ ! -f "$bin" ]; then
        echo "[SKIP] $name (binary not found)"
        echo ""
        return
    fi

    if "$bin"; then
        echo "[PASS] $name"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo "[FAIL] $name (exit code: $?)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
    echo ""
}

run_test "test_ebml" "test_ebml"
run_test "test_avc" "test_avc"
run_test "test_muxer_only" "test_muxer_only"
run_test "test_full_pipeline" "test_full_pipeline"

echo "==================================================================="
echo " Unit Tests: $PASS_COUNT passed, $FAIL_COUNT failed"
echo "==================================================================="
echo ""

# =========================================================================
# STEP 4 — Testes CLI com screen.raw
# =========================================================================
echo "[4/5] CLI Tests..."
echo ""

if [ -z "$RAW_FILE" ]; then
    echo "[SKIP] Nenhum arquivo .raw especificado."
    echo "       Use: ./build_and_test.sh --raw /caminho/para/screen.raw"
    echo ""
elif [ ! -f "$RAW_FILE" ]; then
    echo "[SKIP] Arquivo nao encontrado: $RAW_FILE"
    echo ""
else
    TEST_OUTPUT_DIR="$SCRIPT_DIR/test_output"
    mkdir -p "$TEST_OUTPUT_DIR"

    # Verificar tamanho do arquivo
    RAW_SIZE=$(stat -c%s "$RAW_FILE" 2>/dev/null || stat -f%z "$RAW_FILE" 2>/dev/null)
    FRAME_SIZE=$(( WIDTH * HEIGHT * 3 / 2 ))
    TOTAL_FRAMES=$(( RAW_SIZE / FRAME_SIZE ))

    echo " Input:       $RAW_FILE"
    echo " File size:   $RAW_SIZE bytes"
    echo " Frame size:  $FRAME_SIZE bytes"
    echo " Total frames: $TOTAL_FRAMES"
    echo " Resolution:  ${WIDTH}x${HEIGHT}"
    echo " FPS:         $FPS"
    echo " Bitrate:     $BITRATE kbps"
    echo ""

    if [ $(( RAW_SIZE % FRAME_SIZE )) -ne 0 ]; then
        echo "[WARN] File size is not a multiple of frame size!"
        echo "       Verifique --width e --height."
        echo ""
    fi

    # --- nessmux com encoder AUTO ---
    echo "--- nessmux (encoder: auto) ---"
    OUT_AUTO="$TEST_OUTPUT_DIR/screen_auto.mkv"
    if "$BUILD_DIR/nessmux" "$RAW_FILE" "$OUT_AUTO" \
        --width "$WIDTH" --height "$HEIGHT" --fps "$FPS" --bitrate "$BITRATE" \
        --encoder auto; then
        echo "[PASS] nessmux auto"
    else
        echo "[FAIL] nessmux auto (exit code: $?)"
    fi
    echo ""

    # --- nessmux com encoder x264 (se compilado) ---
    if [ "$USE_X264" = "ON" ]; then
        echo "--- nessmux (encoder: x264) ---"
        OUT_X264="$TEST_OUTPUT_DIR/screen_x264.mkv"
        if "$BUILD_DIR/nessmux" "$RAW_FILE" "$OUT_X264" \
            --width "$WIDTH" --height "$HEIGHT" --fps "$FPS" --bitrate "$BITRATE" \
            --encoder x264; then
            echo "[PASS] nessmux x264"
        else
            echo "[FAIL] nessmux x264 (exit code: $?)"
        fi
        echo ""
    fi

    # --- nessmux_validate nos MKVs gerados ---
    echo "--- nessmux_validate ---"
    echo ""

    for mkv in "$TEST_OUTPUT_DIR"/screen_*.mkv; do
        if [ -f "$mkv" ]; then
            NAME=$(basename "$mkv")
            echo "Validating: $NAME"
            if "$BUILD_DIR/nessmux_validate" "$mkv"; then
                echo "[PASS] validate $NAME"
            else
                echo "[FAIL] validate $NAME"
            fi
            echo ""
        fi
    done
fi

# =========================================================================
# STEP 5 — Benchmark
# =========================================================================
echo "[5/5] Running benchmark..."
echo ""

echo "--- Benchmark (encoder: auto) ---"
"$BUILD_DIR/nessmux_bench" --encoder auto || true
echo ""

if [ "$USE_X264" = "ON" ]; then
    echo "--- Benchmark (encoder: x264) ---"
    "$BUILD_DIR/nessmux_bench" --encoder x264 || true
    echo ""
fi

# =========================================================================
# Resumo final
# =========================================================================
echo ""
echo "==================================================================="
echo " BUILD AND TEST COMPLETE"
echo "==================================================================="
echo ""
echo " Binaries: $BUILD_DIR/"
echo ""

if [ -d "$SCRIPT_DIR/test_output" ]; then
    echo " Output MKVs:"
    ls -lh "$SCRIPT_DIR/test_output"/screen_*.mkv 2>/dev/null | while read line; do
        echo "   $line"
    done
    echo ""
    echo " Dica: abra os MKVs com ffplay ou VLC:"
    echo "   ffplay test_output/screen_auto.mkv"
fi

echo ""
echo "==================================================================="
echo ""