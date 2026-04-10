@echo off
REM ============================================================================
REM NV12 RAW to MKV N.148 Conversion Script
REM Generates two versions: CABAC and CAVLC
REM ============================================================================

setlocal

REM Configuration - adjust as needed
set NESSMUX=build\Release\nessmux.exe
set INPUT=video_nv12.raw
set WIDTH=1920
set HEIGHT=1080
set FPS=60
set BITRATE=25961

REM Check if executable exists
if not exist "%NESSMUX%" (
    echo ERROR: %NESSMUX% not found!
    echo.
    echo Build the project first:
    echo   mkdir build
    echo   cd build
    echo   cmake .. -DNESS_USE_N148=ON
    echo   cmake --build . --config Release
    echo.
    pause
    exit /b 1
)

REM Check if input file exists
if not exist "%INPUT%" (
    echo ERROR: %INPUT% not found!
    echo.
    pause
    exit /b 1
)

echo ============================================================================
echo NessMuxer - NV12 RAW to MKV N.148 Conversion
echo ============================================================================
echo.
echo Input:      %INPUT%
echo Resolution: %WIDTH%x%HEIGHT%
echo FPS:        %FPS%
echo Bitrate:    %BITRATE% kbps
echo.

echo ----------------------------------------------------------------------------
echo [1/2] Converting with CABAC...
echo ----------------------------------------------------------------------------
%NESSMUX% %INPUT% video_n148_cabac.mkv --width %WIDTH% --height %HEIGHT% --fps %FPS% --bitrate %BITRATE% --codec n148 --entropy cabac

if %ERRORLEVEL% neq 0 (
    echo ERROR during CABAC conversion!
    pause
    exit /b 1
)

echo.
echo ----------------------------------------------------------------------------
echo [2/2] Converting with CAVLC...
echo ----------------------------------------------------------------------------
%NESSMUX% %INPUT% video_n148_cavlc.mkv --width %WIDTH% --height %HEIGHT% --fps %FPS% --bitrate %BITRATE% --codec n148 --entropy cavlc

if %ERRORLEVEL% neq 0 (
    echo ERROR during CAVLC conversion!
    pause
    exit /b 1
)

echo.
echo ============================================================================
echo Conversion completed successfully!
echo ============================================================================
echo.
echo Generated files:
echo   - video_n148_cabac.mkv
echo   - video_n148_cavlc.mkv
echo.

pause