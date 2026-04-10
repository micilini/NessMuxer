@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul 2>&1

REM ============================================================
REM NESSMUXER - BENCHMARK N.148 - FASE 11.0
REM ============================================================

cd /d "%~dp0"

REM ============================================================
REM CONFIGURACOES
REM ============================================================
set "INPUT=video_nv12.raw"
set "WIDTH=1920"
set "HEIGHT=1080"
set "FPS=60"
set "BITRATE=25961"
set "REPORT_FILE=n148_benchmark_report.txt"

echo.
echo ==============================================================
echo    NESSMUXER - BENCHMARK N.148 - FASE 11.0
echo    Codec Autoral vs Codecs de Referencia
echo ==============================================================
echo.

REM ============================================================
REM BUSCAR EXECUTAVEL DO NESSMUXER
REM ============================================================
set "NESSMUX="

if exist ".\nessmux.exe" (
    set "NESSMUX=.\nessmux.exe"
    goto :found_exe
)

if exist ".\build\Release\nessmux.exe" (
    set "NESSMUX=.\build\Release\nessmux.exe"
    goto :found_exe
)

if exist ".\build\Debug\nessmux.exe" (
    set "NESSMUX=.\build\Debug\nessmux.exe"
    goto :found_exe
)

if exist ".\build\nessmux.exe" (
    set "NESSMUX=.\build\nessmux.exe"
    goto :found_exe
)

if exist ".\Release\nessmux.exe" (
    set "NESSMUX=.\Release\nessmux.exe"
    goto :found_exe
)

if exist ".\Debug\nessmux.exe" (
    set "NESSMUX=.\Debug\nessmux.exe"
    goto :found_exe
)

echo [ERRO] Nenhum executavel do NessMuxer encontrado.
echo        Compile o projeto primeiro com build_and_test.bat
goto :end

:found_exe
echo [OK] Executavel encontrado: %NESSMUX%
echo.

REM ============================================================
REM VERIFICAR INPUT
REM ============================================================
if not exist "%INPUT%" (
    echo [ERRO] %INPUT% nao encontrado.
    goto :end
)

REM Calcular info do input
for %%A in ("%INPUT%") do set "INPUT_SIZE=%%~zA"
set /a "FRAME_SIZE=%WIDTH% * %HEIGHT% * 3 / 2"
set /a "TOTAL_FRAMES=%INPUT_SIZE% / %FRAME_SIZE%"
set /a "DURATION_SEC=%TOTAL_FRAMES% / %FPS%"

echo   NessMuxer:       %NESSMUX%
echo   Input:           %INPUT%
echo   Tamanho:         %INPUT_SIZE% bytes
echo   Frames:          %TOTAL_FRAMES%
echo   Duracao:         %DURATION_SEC%s
echo   Resolucao:       %WIDTH%x%HEIGHT%
echo   FPS:             %FPS%
echo   Bitrate:         %BITRATE% kbps
echo.

REM Inicializar relatorio
echo ============================================================== > "%REPORT_FILE%"
echo NESSMUXER N.148 - RELATORIO DE BENCHMARK >> "%REPORT_FILE%"
echo ============================================================== >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"
echo Input: %INPUT% >> "%REPORT_FILE%"
echo Resolucao: %WIDTH%x%HEIGHT% @ %FPS% fps >> "%REPORT_FILE%"
echo Bitrate Alvo: %BITRATE% kbps >> "%REPORT_FILE%"
echo Total Frames: %TOTAL_FRAMES% >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"

REM ============================================================
REM ENCODE N.148 CAVLC
REM ============================================================
echo --------------------------------------------------------------
echo [ENCODE] N.148 CAVLC
echo --------------------------------------------------------------

set "OUT_CAVLC=video_n148_cavlc.mkv"
set "SIZE_CAVLC=0"

REM Capturar tempo
for /f "tokens=1-4 delims=:., " %%a in ('echo %time%') do (
    set /a "START_TIME_CAVLC=%%a*3600+%%b*60+%%c"
)

REM Sintaxe correta: nessmux <input> <output> --flags
echo   Comando: %NESSMUX% %INPUT% %OUT_CAVLC% --width %WIDTH% --height %HEIGHT% --fps %FPS% --bitrate %BITRATE% --encoder n148 --codec n148 --entropy cavlc
echo.

"%NESSMUX%" "%INPUT%" "%OUT_CAVLC%" --width %WIDTH% --height %HEIGHT% --fps %FPS% --bitrate %BITRATE% --encoder n148 --codec n148 --entropy cavlc

REM Capturar tempo final
for /f "tokens=1-4 delims=:., " %%a in ('echo %time%') do (
    set /a "END_TIME_CAVLC=%%a*3600+%%b*60+%%c"
)

set /a "ELAPSED_CAVLC_SEC=%END_TIME_CAVLC%-%START_TIME_CAVLC%"
if %ELAPSED_CAVLC_SEC% lss 0 set /a "ELAPSED_CAVLC_SEC+=86400"

if exist "%OUT_CAVLC%" (
    for %%A in ("%OUT_CAVLC%") do set "SIZE_CAVLC=%%~zA"
    set /a "SIZE_CAVLC_MB=!SIZE_CAVLC! / 1048576"
    echo.
    echo   [OK] %OUT_CAVLC%
    echo        Tamanho: !SIZE_CAVLC! bytes - !SIZE_CAVLC_MB! MB
    echo        Tempo: %ELAPSED_CAVLC_SEC%s
) else (
    echo   [FALHA] Arquivo nao gerado
)

echo.

REM ============================================================
REM ENCODE N.148 CABAC
REM ============================================================
echo --------------------------------------------------------------
echo [ENCODE] N.148 CABAC
echo --------------------------------------------------------------

set "OUT_CABAC=video_n148_cabac.mkv"
set "SIZE_CABAC=0"

for /f "tokens=1-4 delims=:., " %%a in ('echo %time%') do (
    set /a "START_TIME_CABAC=%%a*3600+%%b*60+%%c"
)

echo   Comando: %NESSMUX% %INPUT% %OUT_CABAC% --width %WIDTH% --height %HEIGHT% --fps %FPS% --bitrate %BITRATE% --encoder n148 --codec n148 --entropy cabac
echo.

"%NESSMUX%" "%INPUT%" "%OUT_CABAC%" --width %WIDTH% --height %HEIGHT% --fps %FPS% --bitrate %BITRATE% --encoder n148 --codec n148 --entropy cabac

for /f "tokens=1-4 delims=:., " %%a in ('echo %time%') do (
    set /a "END_TIME_CABAC=%%a*3600+%%b*60+%%c"
)

set /a "ELAPSED_CABAC_SEC=%END_TIME_CABAC%-%START_TIME_CABAC%"
if %ELAPSED_CABAC_SEC% lss 0 set /a "ELAPSED_CABAC_SEC+=86400"

if exist "%OUT_CABAC%" (
    for %%A in ("%OUT_CABAC%") do set "SIZE_CABAC=%%~zA"
    set /a "SIZE_CABAC_MB=!SIZE_CABAC! / 1048576"
    echo.
    echo   [OK] %OUT_CABAC%
    echo        Tamanho: !SIZE_CABAC! bytes - !SIZE_CABAC_MB! MB
    echo        Tempo: %ELAPSED_CABAC_SEC%s
) else (
    echo   [FALHA] Arquivo nao gerado
)

echo.

REM ============================================================
REM COMPARACAO COM REFERENCIAS
REM ============================================================
echo ==============================================================
echo    COMPARACAO N.148 vs REFERENCIAS
echo ==============================================================
echo.

echo COMPARACAO DE TAMANHOS >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"

echo   Codec                     Tamanho          MB        Ratio
echo   ------------------------  ---------------  --------  ------

REM Listar todos os videos
for %%F in (video_*.mkv) do call :show_video "%%F"

echo.

REM ============================================================
REM ANALISE DO GAP
REM ============================================================
if exist "video_h264_cabac.mkv" (
    for %%A in ("video_h264_cabac.mkv") do set "SIZE_H264=%%~zA"
    
    if !SIZE_CAVLC! gtr 0 (
        set /a "GAP_CAVLC=(!SIZE_CAVLC! - !SIZE_H264!) * 100 / !SIZE_H264!"
        echo   Gap N.148 CAVLC vs H.264 CABAC: !GAP_CAVLC!%%
    )
    
    if !SIZE_CABAC! gtr 0 (
        set /a "GAP_CABAC=(!SIZE_CABAC! - !SIZE_H264!) * 100 / !SIZE_H264!"
        echo   Gap N.148 CABAC vs H.264 CABAC: !GAP_CABAC!%%
    )
)

if exist "video_h264_cavlc.mkv" (
    for %%A in ("video_h264_cavlc.mkv") do set "SIZE_H264_CAVLC=%%~zA"
    
    if !SIZE_CAVLC! gtr 0 (
        set /a "GAP_VS_CAVLC=(!SIZE_CAVLC! - !SIZE_H264_CAVLC!) * 100 / !SIZE_H264_CAVLC!"
        echo   Gap N.148 CAVLC vs H.264 CAVLC: !GAP_VS_CAVLC!%%
    )
)

echo.

REM ============================================================
REM RANKING FINAL
REM ============================================================
echo   RANKING - ordenado por tamanho:
echo.

powershell -NoProfile -Command "$files = Get-ChildItem -LiteralPath . -File | Where-Object { $_.Name -like 'video_*.mkv' } | Sort-Object Length; $rank = 1; $files | ForEach-Object { $marker = if ($_.Name -like '*n148*') { ' <-- N.148' } else { '' }; '{0}. {1} ({2:N2} MB){3}' -f $rank, $_.Name, ($_.Length / 1MB), $marker; $rank++ }"

echo.

echo ============================================================== >> "%REPORT_FILE%"
echo CONCLUSAO DA FASE 11.0 >> "%REPORT_FILE%"
echo ============================================================== >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"
echo Os dados acima estabelecem a baseline oficial do N.148. >> "%REPORT_FILE%"
echo Proximo passo: FASE 11.1 - Motion Estimation >> "%REPORT_FILE%"

echo ==============================================================
echo    BENCHMARK N.148 CONCLUIDO
echo ==============================================================
echo.
echo   Relatorio: %REPORT_FILE%
echo.

goto :end

REM ============================================================
REM SUBROTINA: Mostrar info de um video
REM ============================================================
:show_video
set "VFILE=%~nx1"
for %%A in ("%~1") do set "VSIZE=%%~zA"
if defined VSIZE (
    set /a "VMB=!VSIZE! / 1048576"
    set /a "VRATIO=!VSIZE! * 100 / %INPUT_SIZE%"
    echo   !VFILE!    !VSIZE!    !VMB! MB    !VRATIO!%%
    echo !VFILE!: !VSIZE! bytes - !VRATIO!%% >> "%REPORT_FILE%"
)
goto :eof

:end
pause