@echo off
setlocal EnableExtensions

if "%~1"=="" goto :usage
if "%~2"=="" goto :usage
if "%~3"=="" goto :usage

set "INPUT_MKV=%~1"
set "FRAME_INDEX=%~2"
set "OUT_FMT=%~3"
set "OUTPUT_FILE=%~4"
set "SCRIPT_DIR=%~dp0"
set "FRAMEGRAB_EXE=%SCRIPT_DIR%build\Release\n148_framegrab.exe"

if not exist "%FRAMEGRAB_EXE%" (
    echo [FAIL] Could not find n148_framegrab.exe at:
    echo        %FRAMEGRAB_EXE%
    exit /b 1
)

if /I not "%OUT_FMT%"=="bmp" if /I not "%OUT_FMT%"=="png" if /I not "%OUT_FMT%"=="jpg" if /I not "%OUT_FMT%"=="jpeg" (
    echo [FAIL] Unsupported output format: %OUT_FMT%
    exit /b 1
)

if "%OUTPUT_FILE%"=="" (
    set "OUTPUT_FILE=%~dp1frame_%FRAME_INDEX%.%OUT_FMT%"
)

for %%I in ("%OUTPUT_FILE%") do (
    set "BMP_FILE=%%~dpnI.bmp"
)

if /I "%OUT_FMT%"=="bmp" set "BMP_FILE=%OUTPUT_FILE%"

echo [1/2] Extracting decoded frame %FRAME_INDEX% to BMP...
"%FRAMEGRAB_EXE%" "%INPUT_MKV%" %FRAME_INDEX% "%BMP_FILE%"
if errorlevel 1 (
    echo [FAIL] Frame extraction failed.
    exit /b 1
)

if not exist "%BMP_FILE%" (
    echo [FAIL] Frame extraction reported success, but BMP was not found:
    echo        %BMP_FILE%
    echo [TIP] Run the exe directly to see its own diagnostics:
    echo       "%FRAMEGRAB_EXE%" "%INPUT_MKV%" %FRAME_INDEX% "%BMP_FILE%"
    exit /b 1
)

if /I "%OUT_FMT%"=="bmp" (
    echo [OK] Wrote %BMP_FILE%
    exit /b 0
)

echo [2/2] Converting BMP to %OUT_FMT% using PowerShell...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$bmp=[System.IO.Path]::GetFullPath('%BMP_FILE%'); $out=[System.IO.Path]::GetFullPath('%OUTPUT_FILE%'); Add-Type -AssemblyName System.Drawing; if (!(Test-Path -LiteralPath $bmp)) { throw 'BMP not found: ' + $bmp }; $img=[System.Drawing.Image]::FromFile($bmp); try { if ('%OUT_FMT%'.ToLower() -eq 'png') { $fmt=[System.Drawing.Imaging.ImageFormat]::Png } else { $fmt=[System.Drawing.Imaging.ImageFormat]::Jpeg }; $img.Save($out,$fmt) } finally { if ($img) { $img.Dispose() } }"
if errorlevel 1 (
    echo [FAIL] PowerShell conversion failed. Keeping BMP at %BMP_FILE%
    exit /b 1
)

del /q "%BMP_FILE%" >nul 2>&1
echo [OK] Wrote %OUTPUT_FILE%
exit /b 0

:usage
echo Usage:
echo   %~n0 ^<input.mkv^> ^<frame_index^> ^<bmp^|png^|jpg^> [output_file]
exit /b 1
