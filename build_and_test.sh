#!/bin/bash
# =========================================================================
# NessMuxer - Build & Test Script (Linux/macOS)
# =========================================================================
#
# Usage:
#   ./build_and_test.sh                    (default build: x264 + N.148)
#   ./build_and_test.sh --no-x264          (build without x264 - only N.148)
#   ./build_and_test.sh --raw <path>       (specify .raw file for CLI tests)
#   ./build_and_test.sh --width <w>        (frame width, default 1920)
#   ./build_and_test.sh --height <h>       (frame height, default 1080)
#   ./build_and_test.sh --fps <f>          (framerate, default 30)
#   ./build_and_test.sh --bitrate <b>      (bitrate in kbps, default 6000)
#
# Requirements:
#   sudo apt install build-essential cmake pkg-config libx264-dev
#
# =========================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="$SCRIPT_DIR/build"
USE_X264="ON"
USE_N148="ON"
RAW_FILE=""
WIDTH=1920
HEIGHT=1080
FPS=30
BITRATE=6000

# =========================================================================
# Parse arguments
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
            echo "Usage: ./build_and_test.sh [--no-x264] [--raw <path>] [--width W] [--height H] [--fps F] [--bitrate B]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
            exit 1
            ;;
    esac
done

echo ""
echo "==================================================================="
echo " NessMuxer - Build & Test Script (Linux/macOS)"
echo "==================================================================="
echo " X264:     $USE_X264"
echo " N.148:    $USE_N148"
echo " Raw file: ${RAW_FILE:-(none)}"
if [ -n "$RAW_FILE" ]; then
    echo " Resolution: ${WIDTH}x${HEIGHT} @ ${FPS}fps, ${BITRATE}kbps"
fi
echo "==================================================================="
echo ""

# =========================================================================
# STEP 0 - Check dependencies
# =========================================================================
echo "[0/5] Checking dependencies..."
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
    echo "[FAIL] Missing dependencies:$MISSING"
    echo ""
    echo "  Install with:"
    echo "    sudo apt update"
    echo "    sudo apt install build-essential cmake pkg-config libx264-dev"
    echo ""
    exit 1
fi

echo "[OK] All dependencies found."
if [ "$USE_X264" = "ON" ]; then
    X264_VERSION=$(pkg-config --modversion x264 2>/dev/null || echo "?")
    echo "     x264 version: $X264_VERSION"
fi
echo ""

# =========================================================================
# STEP 1 - CMake Configure
# =========================================================================
echo "[1/5] CMake Configure..."
echo ""

cmake -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DNESS_USE_X264="$USE_X264" \
      -DNESS_USE_N148="$USE_N148" \
      "$SCRIPT_DIR"
echo ""
echo "[OK] CMake configure done."
echo ""

# =========================================================================
# STEP 2 - Build
# =========================================================================
echo "[2/5] Building..."
echo ""

cmake --build "$BUILD_DIR" --config Release -j$(nproc)
echo ""
echo "[OK] Build succeeded."
echo ""

# Check binaries
echo "Checking binaries:"
BINARIES=(
    "libNessMuxer.so"
    "test_ebml"
    "test_avc"
    "test_muxer_only"
    "test_full_pipeline"
    "nessmux"
    "nessmux_validate"
    "nessmux_bench"
    "test_n148_codec"
    "test_n148_mux"
    "test_n148_encoder"
    "test_n148_roundtrip"
    "test_n148_interpolation"
    "test_n148_reorder"
    "test_n148_gop_planner"
    "test_n148_ratecontrol"
    "test_n148_profiles"
    "test_n148_metrics"
    "test_n148_cabac"
    "test_n148_cabac_engine"
    "test_n148_cabac_slice_roundtrip"
    "test_n148_cabac_conformance_small"
    "test_n148_phase7_epic"
    "test_n148_tools"
)

for bin in "${BINARIES[@]}"; do
    if [ -f "$BUILD_DIR/$bin" ]; then
        echo "  [OK] $bin"
    else
        echo "  [INFO] $bin - optional"
    fi
done
echo ""

# =========================================================================
# STEP 3 - Unit tests
# =========================================================================
echo "[3/5] Running unit tests..."
echo ""

PASS_COUNT=0
FAIL_COUNT=0

# Ensure .so is accessible for tests
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

# Core tests
run_test "test_ebml" "test_ebml"
run_test "test_avc" "test_avc"
run_test "test_muxer_only" "test_muxer_only"
run_test "test_full_pipeline" "test_full_pipeline"

# N.148 tests
run_test "test_n148_codec" "test_n148_codec"
run_test "test_n148_mux" "test_n148_mux"
run_test "test_n148_encoder" "test_n148_encoder"
run_test "test_n148_roundtrip" "test_n148_roundtrip"
run_test "test_n148_interpolation" "test_n148_interpolation"
run_test "test_n148_reorder" "test_n148_reorder"
run_test "test_n148_gop_planner" "test_n148_gop_planner"
run_test "test_n148_ratecontrol" "test_n148_ratecontrol"
run_test "test_n148_profiles" "test_n148_profiles"
run_test "test_n148_metrics" "test_n148_metrics"
run_test "test_n148_cabac" "test_n148_cabac"
run_test "test_n148_cabac_engine" "test_n148_cabac_engine"
run_test "test_n148_cabac_slice_roundtrip" "test_n148_cabac_slice_roundtrip"
run_test "test_n148_cabac_conformance_small" "test_n148_cabac_conformance_small"
run_test "test_n148_phase7_epic" "test_n148_phase7_epic"
run_test "test_n148_tools" "test_n148_tools"

# Validate generated MKVs
if [ -f "test_n148_mux.mkv" ]; then
    echo "--- nessmux_validate test_n148_mux.mkv ---"
    if "$BUILD_DIR/nessmux_validate" test_n148_mux.mkv; then
        echo "[PASS] validate test_n148_mux.mkv"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo "[FAIL] validate test_n148_mux.mkv"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
    echo ""
fi

if [ -f "test_n148_mux_cabac.mkv" ]; then
    echo "--- nessmux_validate test_n148_mux_cabac.mkv ---"
    if "$BUILD_DIR/nessmux_validate" test_n148_mux_cabac.mkv; then
        echo "[PASS] validate test_n148_mux_cabac.mkv"
    else
        echo "[FAIL] validate test_n148_mux_cabac.mkv"
    fi
    echo ""
fi

echo "==================================================================="
echo " Unit Tests: $PASS_COUNT passed, $FAIL_COUNT failed"
echo "==================================================================="
echo ""

# =========================================================================
# STEP 4 - CLI tests with screen.raw
# =========================================================================
echo "[4/5] CLI Tests..."
echo ""

if [ -z "$RAW_FILE" ]; then
    echo "[SKIP] No .raw file specified."
    echo "       Use: ./build_and_test.sh --raw /path/to/screen.raw"
    echo ""
elif [ ! -f "$RAW_FILE" ]; then
    echo "[SKIP] File not found: $RAW_FILE"
    echo ""
else
    TEST_OUTPUT_DIR="$SCRIPT_DIR/test_output"
    mkdir -p "$TEST_OUTPUT_DIR"

    # Check file size
    RAW_SIZE=$(stat -c%s "$RAW_FILE" 2>/dev/null || stat -f%z "$RAW_FILE" 2>/dev/null)
    FRAME_SIZE=$(( WIDTH * HEIGHT * 3 / 2 ))
    TOTAL_FRAMES=$(( RAW_SIZE / FRAME_SIZE ))

    echo " Input:        $RAW_FILE"
    echo " File size:    $RAW_SIZE bytes"
    echo " Frame size:   $FRAME_SIZE bytes"
    echo " Total frames: $TOTAL_FRAMES"
    echo " Resolution:   ${WIDTH}x${HEIGHT}"
    echo " FPS:          $FPS"
    echo " Bitrate:      $BITRATE kbps"
    echo ""

    if [ $(( RAW_SIZE % FRAME_SIZE )) -ne 0 ]; then
        echo "[WARN] File size is not a multiple of frame size!"
        echo "       Check --width and --height."
        echo ""
    fi

    # --- nessmux with AUTO encoder ---
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

    # --- nessmux with N148 encoder (CAVLC) ---
    echo "--- nessmux (encoder: n148, codec: n148, entropy: cavlc) ---"
    OUT_N148_CAVLC="$TEST_OUTPUT_DIR/screen_n148_cavlc.mkv"
    if "$BUILD_DIR/nessmux" "$RAW_FILE" "$OUT_N148_CAVLC" \
        --width "$WIDTH" --height "$HEIGHT" --fps "$FPS" --bitrate "$BITRATE" \
        --encoder n148 --codec n148 --entropy cavlc; then
        echo "[PASS] nessmux n148 cavlc"
    else
        echo "[FAIL] nessmux n148 cavlc (exit code: $?)"
    fi
    echo ""

    # --- nessmux with N148 encoder (CABAC) ---
    echo "--- nessmux (encoder: n148, codec: n148, entropy: cabac) ---"
    OUT_N148_CABAC="$TEST_OUTPUT_DIR/screen_n148_cabac.mkv"
    if "$BUILD_DIR/nessmux" "$RAW_FILE" "$OUT_N148_CABAC" \
        --width "$WIDTH" --height "$HEIGHT" --fps "$FPS" --bitrate "$BITRATE" \
        --encoder n148 --codec n148 --entropy cabac; then
        echo "[PASS] nessmux n148 cabac"
    else
        echo "[FAIL] nessmux n148 cabac (exit code: $?)"
    fi
    echo ""

    # --- nessmux with x264 encoder (if compiled) ---
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
    else
        echo "[SKIP] nessmux x264 (not compiled with NESS_USE_X264)"
        echo ""
    fi

    # --- nessmux_validate on generated MKVs ---
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
# STEP 5 - Benchmark
# =========================================================================
echo "[5/5] Running benchmark..."
echo ""

echo "--- Benchmark (encoder: auto) ---"
"$BUILD_DIR/nessmux_bench" --encoder auto || true
echo ""

echo "--- Benchmark (encoder: n148) ---"
"$BUILD_DIR/nessmux_bench" --encoder n148 || true
echo ""

if [ "$USE_X264" = "ON" ]; then
    echo "--- Benchmark (encoder: x264) ---"
    "$BUILD_DIR/nessmux_bench" --encoder x264 || true
    echo ""
fi

# =========================================================================
# Final summary
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
    echo " Tip: open the MKVs with ffplay or VLC:"
    echo "   ffplay test_output/screen_auto.mkv"
fi

echo ""
echo "==================================================================="
echo ""