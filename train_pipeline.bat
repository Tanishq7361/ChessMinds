@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM   ChessMinds Incremental Training & Tuning Pipeline (Batch)
REM ============================================================

set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

if "%~1"=="" goto help
if "%~1"=="help" goto help
if "%~1"=="-h" goto help
if "%~1"=="--help" goto help

if "%~1"=="stats" (
    py tools/pgn_integrate.py --stats-only --dataset data/positions/quiet_positions.txt
    goto done
)

if "%~1"=="integrate" (
    if "%~2"=="" (
        echo Error: Missing input PGN path.
        echo Usage: train_pipeline.bat integrate path\to\games.pgn
        exit /b 1
    )
    py tools/pgn_integrate.py --input "%~2" --dataset data/positions/quiet_positions.txt --min-elo 2000
    goto done
)

if "%~1"=="export" (
    set OUT_FILE=%~2
    if "!OUT_FILE!"=="" set OUT_FILE=data\params\tuned_params_latest.json
    py tools/texel_fast.py --export-params "!OUT_FILE!"
    goto done
)

if "%~1"=="tune" (
    set ITERS=%~2
    if "!ITERS!"=="" set ITERS=5
    set MAX_POS=%~3
    if "!MAX_POS!"=="" set MAX_POS=100000
    py tools/texel_fast.py --positions data/positions/quiet_positions.txt --iterations !ITERS! --max-positions !MAX_POS! --output-params data/params/tuned_params_latest.json
    goto done
)

if "%~1"=="full" (
    if "%~2"=="" (
        echo Error: Missing input PGN path.
        echo Usage: train_pipeline.bat full path\to\games.pgn [iterations] [max_train_pos]
        exit /b 1
    )
    set ITERS=%~3
    if "!ITERS!"=="" set ITERS=5
    set MAX_POS=%~4
    if "!MAX_POS!"=="" set MAX_POS=100000
    py tools/pgn_integrate.py --input "%~2" --dataset data/positions/quiet_positions.txt --min-elo 2000
    if errorlevel 1 exit /b 1
    py tools/texel_fast.py --positions data/positions/quiet_positions.txt --iterations !ITERS! --max-positions !MAX_POS! --output-params data/params/tuned_params_latest.json
    goto done
)

echo Unknown action: %~1
echo.

:help
echo ChessMinds Training Pipeline Runner
echo.
echo Usage:
echo   train_pipeline.bat stats
echo   train_pipeline.bat integrate ^<pgn_file^>
echo   train_pipeline.bat export [output_json]
echo   train_pipeline.bat tune [iterations] [max_train_pos]
echo   train_pipeline.bat full ^<pgn_file^> [iterations] [max_train_pos]
echo.
exit /b 0

:done
echo.
echo [OK] Command completed.
exit /b 0
