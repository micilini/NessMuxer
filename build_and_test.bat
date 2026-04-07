@echo off
setlocal enabledelayedexpansion
chdir /d "%~dp0"

:: =========================================================================
:: NessMuxer - Build, Test & CLI Script (Windows 11)
:: =========================================================================
::
:: Uso:
::   build_and_test.bat              (build default: Media Foundation only)
::   build_and_test.bat x264         (build com x264)
::   build_and_test.bat nvenc        (build com NVENC)
::   build_and_test.bat all          (build com x264 + NVENC)
::
:: Requisitos:
::   - Visual Studio 2019/2022 com C/C++ desktop workload
::   - CMake 3.15+ no PATH
::
:: =========================================================================

set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build"
set "RELEASE_DIR=%BUILD_DIR%\Release"

:: Paths dos headers third-party (relativos ao projeto)
set "X264_HEADERS=%PROJECT_DIR%third_party\x264\include"
set "NVENC_HEADERS=%PROJECT_DIR%third_party\nvenc\include"

:: Arquivo raw para teste CLI
set "RAW_FILE=C:\Users\sdanz\Documents\NessStudio\Recordings\20260405_210620\screen.raw"

:: Diretorio de saida dos testes
set "TEST_OUTPUT_DIR=%PROJECT_DIR%test_output"

:: =========================================================================
:: Parse argumento
:: =========================================================================
set "BUILD_MODE=default"
if /i "%~1"=="x264"  set "BUILD_MODE=x264"
if /i "%~1"=="nvenc" set "BUILD_MODE=nvenc"
if /i "%~1"=="all"   set "BUILD_MODE=all"

set "USE_X264=OFF"
set "USE_NVENC=OFF"

if "%BUILD_MODE%"=="x264" (
    set "USE_X264=ON"
)
if "%BUILD_MODE%"=="nvenc" (
    set "USE_NVENC=ON"
)
if "%BUILD_MODE%"=="all" (
    set "USE_X264=ON"
    set "USE_NVENC=ON"
)

echo.
echo ===================================================================
echo  NessMuxer - Build ^& Test Script
echo ===================================================================
echo  Mode:     %BUILD_MODE%
echo  X264:     %USE_X264%
echo  NVENC:    %USE_NVENC%
echo  Raw file: %RAW_FILE%
echo ===================================================================
echo.

:: =========================================================================
:: STEP 1 — CMake Configure
:: =========================================================================
echo [1/5] CMake Configure...
echo.

set "CMAKE_ARGS=-B "%BUILD_DIR%" -A x64"
set "CMAKE_ARGS=%CMAKE_ARGS% -DNESS_USE_X264=%USE_X264%"
set "CMAKE_ARGS=%CMAKE_ARGS% -DNESS_USE_NVENC=%USE_NVENC%"

if "%USE_X264%"=="ON" (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DNESS_X264_INCLUDE_DIR="%X264_HEADERS%""
)
if "%USE_NVENC%"=="ON" (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DNESS_NVENC_INCLUDE_DIR="%NVENC_HEADERS%""
)

cmake %CMAKE_ARGS%
if %errorlevel% neq 0 (
    echo.
    echo [FAIL] CMake configure failed!
    exit /b 1
)
echo.
echo [OK] CMake configure done.
echo.

:: =========================================================================
:: STEP 2 — Build Release
:: =========================================================================
echo [2/5] Building Release...
echo.

cmake --build "%BUILD_DIR%" --config Release
if %errorlevel% neq 0 (
    echo.
    echo [FAIL] Build failed!
    exit /b 1
)
echo.
echo [OK] Build succeeded.
echo.

:: Verificar que os executaveis existem
echo Checking binaries:
if exist "%RELEASE_DIR%\NessMuxer.dll"      (echo   [OK] NessMuxer.dll)      else (echo   [MISSING] NessMuxer.dll)
if exist "%RELEASE_DIR%\test_ebml.exe"       (echo   [OK] test_ebml.exe)       else (echo   [MISSING] test_ebml.exe)
if exist "%RELEASE_DIR%\test_avc.exe"        (echo   [OK] test_avc.exe)        else (echo   [MISSING] test_avc.exe)
if exist "%RELEASE_DIR%\test_encoder.exe"    (echo   [OK] test_encoder.exe)    else (echo   [MISSING] test_encoder.exe)
if exist "%RELEASE_DIR%\test_muxer_only.exe" (echo   [OK] test_muxer_only.exe) else (echo   [MISSING] test_muxer_only.exe)
if exist "%RELEASE_DIR%\test_full_pipeline.exe" (echo   [OK] test_full_pipeline.exe) else (echo   [MISSING] test_full_pipeline.exe)
if exist "%RELEASE_DIR%\nessmux.exe"         (echo   [OK] nessmux.exe)         else (echo   [MISSING] nessmux.exe)
if exist "%RELEASE_DIR%\nessmux_validate.exe" (echo   [OK] nessmux_validate.exe) else (echo   [MISSING] nessmux_validate.exe)
if exist "%RELEASE_DIR%\nessmux_bench.exe"   (echo   [OK] nessmux_bench.exe)   else (echo   [MISSING] nessmux_bench.exe)
if exist "%RELEASE_DIR%\test_n148_codec.exe" (echo   [OK] test_n148_codec.exe) else (echo   [INFO] test_n148_codec.exe - optional)
echo.

:: =========================================================================
:: STEP 3 — Testes unitarios
:: =========================================================================
echo [3/5] Running unit tests...
echo.

set "PASS_COUNT=0"
set "FAIL_COUNT=0"

echo --- test_ebml ---
"%RELEASE_DIR%\test_ebml.exe"
if %errorlevel% equ 0 (
    echo [PASS] test_ebml
    set /a PASS_COUNT+=1
) else (
    echo [FAIL] test_ebml (exit code: %errorlevel%)
    set /a FAIL_COUNT+=1
)
echo.

echo --- test_avc ---
"%RELEASE_DIR%\test_avc.exe"
if %errorlevel% equ 0 (
    echo [PASS] test_avc
    set /a PASS_COUNT+=1
) else (
    echo [FAIL] test_avc (exit code: %errorlevel%)
    set /a FAIL_COUNT+=1
)
echo.

echo --- test_encoder ---
"%RELEASE_DIR%\test_encoder.exe"
if %errorlevel% equ 0 (
    echo [PASS] test_encoder
    set /a PASS_COUNT+=1
) else (
    echo [FAIL] test_encoder (exit code: %errorlevel%)
    set /a FAIL_COUNT+=1
)
echo.

echo --- test_muxer_only ---
"%RELEASE_DIR%\test_muxer_only.exe"
if %errorlevel% equ 0 (
    echo [PASS] test_muxer_only
    set /a PASS_COUNT+=1
) else (
    echo [FAIL] test_muxer_only (exit code: %errorlevel%)
    set /a FAIL_COUNT+=1
)
echo.

echo --- test_full_pipeline ---
"%RELEASE_DIR%\test_full_pipeline.exe"
if %errorlevel% equ 0 (
    echo [PASS] test_full_pipeline
    set /a PASS_COUNT+=1
) else (
    echo [FAIL] test_full_pipeline (exit code: %errorlevel%)
    set /a FAIL_COUNT+=1
)
echo.

echo --- test_n148_codec ---
"%RELEASE_DIR%\test_n148_codec.exe"
if %errorlevel% equ 0 (
    echo [PASS] test_n148_codec
    set /a PASS_COUNT+=1
) else (
    echo [FAIL] test_n148_codec
    set /a FAIL_COUNT+=1
)
echo.

echo ===================================================================
echo  Unit Tests: %PASS_COUNT% passed, %FAIL_COUNT% failed
echo ===================================================================
echo.

:: =========================================================================
:: STEP 4 — Testes CLI com screen.raw
:: =========================================================================
echo [4/5] CLI Tests with screen.raw...
echo.

:: Verificar se o arquivo raw existe
if not exist "%RAW_FILE%" (
    echo [SKIP] Raw file not found: %RAW_FILE%
    echo        Pulando testes CLI.
    goto :benchmark
)

:: Criar diretorio de saida
if not exist "%TEST_OUTPUT_DIR%" mkdir "%TEST_OUTPUT_DIR%"

:: Descobrir o tamanho do arquivo raw para inferir quantos frames tem
:: Vamos assumir que o NessStudio grava em 1920x1080 NV12 a 30fps
:: frame_size = 1920 * 1080 * 3 / 2 = 3110400 bytes
:: Se a resolucao for diferente, ajuste aqui:
set "WIDTH=1920"
set "HEIGHT=1080"
set "FPS=30"
set "BITRATE=6000"

echo  Resolution: %WIDTH%x%HEIGHT%
echo  FPS:        %FPS%
echo  Bitrate:    %BITRATE% kbps
echo  Input:      %RAW_FILE%
echo.

:: --- CLI Test: nessmux com encoder AUTO ---
echo --- nessmux (encoder: auto) ---
set "OUT_AUTO=%TEST_OUTPUT_DIR%\screen_auto.mkv"
"%RELEASE_DIR%\nessmux.exe" "%RAW_FILE%" "%OUT_AUTO%" --width %WIDTH% --height %HEIGHT% --fps %FPS% --bitrate %BITRATE% --encoder auto
if %errorlevel% equ 0 (
    echo [PASS] nessmux auto
) else (
    echo [FAIL] nessmux auto (exit code: %errorlevel%)
)
echo.

:: --- CLI Test: nessmux com encoder MF ---
echo --- nessmux (encoder: mf) ---
set "OUT_MF=%TEST_OUTPUT_DIR%\screen_mf.mkv"
"%RELEASE_DIR%\nessmux.exe" "%RAW_FILE%" "%OUT_MF%" --width %WIDTH% --height %HEIGHT% --fps %FPS% --bitrate %BITRATE% --encoder mf
if %errorlevel% equ 0 (
    echo [PASS] nessmux mf
) else (
    echo [FAIL] nessmux mf (exit code: %errorlevel%)
)
echo.

:: --- CLI Test: nessmux com encoder x264 (se compilado) ---
if "%USE_X264%"=="ON" (
    echo --- nessmux (encoder: x264) ---
    set "OUT_X264=%TEST_OUTPUT_DIR%\screen_x264.mkv"
    "%RELEASE_DIR%\nessmux.exe" "%RAW_FILE%" "!OUT_X264!" --width %WIDTH% --height %HEIGHT% --fps %FPS% --bitrate %BITRATE% --encoder x264
    if !errorlevel! equ 0 (
        echo [PASS] nessmux x264
    ) else (
        echo [FAIL] nessmux x264 (exit code: !errorlevel!)
    )
    echo.
) else (
    echo [SKIP] nessmux x264 (not compiled with NESS_USE_X264)
    echo.
)

:: --- CLI Test: nessmux com encoder nvenc (se compilado) ---
if "%USE_NVENC%"=="ON" (
    echo --- nessmux (encoder: nvenc) ---
    set "OUT_NVENC=%TEST_OUTPUT_DIR%\screen_nvenc.mkv"
    "%RELEASE_DIR%\nessmux.exe" "%RAW_FILE%" "!OUT_NVENC!" --width %WIDTH% --height %HEIGHT% --fps %FPS% --bitrate %BITRATE% --encoder nvenc
    if !errorlevel! equ 0 (
        echo [PASS] nessmux nvenc
    ) else (
        echo [FAIL] nessmux nvenc (exit code: !errorlevel!)
    )
    echo.
) else (
    echo [SKIP] nessmux nvenc (not compiled with NESS_USE_NVENC)
    echo.
)

:: --- CLI Test: nessmux_validate nos MKVs gerados ---
echo --- nessmux_validate ---
echo.

if exist "%OUT_AUTO%" (
    echo Validating: screen_auto.mkv
    "%RELEASE_DIR%\nessmux_validate.exe" "%OUT_AUTO%"
    if %errorlevel% equ 0 (echo [PASS] validate auto) else (echo [FAIL] validate auto)
    echo.
)

if exist "%OUT_MF%" (
    echo Validating: screen_mf.mkv
    "%RELEASE_DIR%\nessmux_validate.exe" "%OUT_MF%"
    if %errorlevel% equ 0 (echo [PASS] validate mf) else (echo [FAIL] validate mf)
    echo.
)

if "%USE_X264%"=="ON" if exist "%TEST_OUTPUT_DIR%\screen_x264.mkv" (
    echo Validating: screen_x264.mkv
    "%RELEASE_DIR%\nessmux_validate.exe" "%TEST_OUTPUT_DIR%\screen_x264.mkv"
    if !errorlevel! equ 0 (echo [PASS] validate x264) else (echo [FAIL] validate x264)
    echo.
)

if "%USE_NVENC%"=="ON" if exist "%TEST_OUTPUT_DIR%\screen_nvenc.mkv" (
    echo Validating: screen_nvenc.mkv
    "%RELEASE_DIR%\nessmux_validate.exe" "%TEST_OUTPUT_DIR%\screen_nvenc.mkv"
    if !errorlevel! equ 0 (echo [PASS] validate nvenc) else (echo [FAIL] validate nvenc)
    echo.
)

:: =========================================================================
:: STEP 5 — Benchmark
:: =========================================================================
:benchmark
echo [5/5] Running benchmark...
echo.

echo --- Benchmark (encoder: auto) ---
"%RELEASE_DIR%\nessmux_bench.exe" --encoder auto
echo.

if "%USE_X264%"=="ON" (
    echo --- Benchmark (encoder: x264) ---
    "%RELEASE_DIR%\nessmux_bench.exe" --encoder x264
    echo.
)

if "%USE_NVENC%"=="ON" (
    echo --- Benchmark (encoder: nvenc) ---
    "%RELEASE_DIR%\nessmux_bench.exe" --encoder nvenc
    echo.
)

:: =========================================================================
:: Resumo final
:: =========================================================================
echo.
echo ===================================================================
echo  BUILD AND TEST COMPLETE
echo ===================================================================
echo.
echo  Build mode: %BUILD_MODE%
echo  Binaries:   %RELEASE_DIR%
echo.
if exist "%TEST_OUTPUT_DIR%\screen_auto.mkv" (
    echo  Output MKVs:
    if exist "%TEST_OUTPUT_DIR%\screen_auto.mkv"  echo    - %TEST_OUTPUT_DIR%\screen_auto.mkv
    if exist "%TEST_OUTPUT_DIR%\screen_mf.mkv"    echo    - %TEST_OUTPUT_DIR%\screen_mf.mkv
    if exist "%TEST_OUTPUT_DIR%\screen_x264.mkv"  echo    - %TEST_OUTPUT_DIR%\screen_x264.mkv
    if exist "%TEST_OUTPUT_DIR%\screen_nvenc.mkv"  echo    - %TEST_OUTPUT_DIR%\screen_nvenc.mkv
    echo.
    echo  Dica: abra os MKVs com ffplay ou VLC para verificar visualmente:
    echo    ffplay "%TEST_OUTPUT_DIR%\screen_auto.mkv"
)
echo.
echo ===================================================================
echo.

endlocal