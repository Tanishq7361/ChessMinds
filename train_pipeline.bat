@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM   ChessMinds Incremental Training & Queue Pipeline (Batch)
REM ============================================================

set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

if "%~1"=="" goto help
if "%~1"=="help" goto help
if "%~1"=="-h" goto help
if "%~1"=="--help" goto help

if "%~1"=="status" (
    py tools\batch_pipeline.py status
    goto done
)

if "%~1"=="extract" (
    if "%~2"=="" (
        py tools\batch_pipeline.py extract
    ) else (
        py tools\batch_pipeline.py extract --input "%~2"
    )
    goto done
)

if "%~1"=="train-batches" (
    set BATCH_SIZE=%~2
    if "!BATCH_SIZE!"=="" set BATCH_SIZE=100000
    set ITERS=%~3
    if "!ITERS!"=="" set ITERS=3
    py tools\batch_pipeline.py train-batches --batch-size !BATCH_SIZE! --iterations !ITERS!
    goto done
)

if "%~1"=="resume" (
    set BATCH_SIZE=%~2
    if "!BATCH_SIZE!"=="" set BATCH_SIZE=100000
    set ITERS=%~3
    if "!ITERS!"=="" set ITERS=3
    py tools\batch_pipeline.py resume --batch-size !BATCH_SIZE! --iterations !ITERS!
    goto done
)

if "%~1"=="cleanup" (
    if "%~2"=="" (
        echo Error: Missing target PGN filename.
        echo Usage: train_pipeline.bat cleanup ^<filename.pgn^>
        exit /b 1
    )
    py tools\batch_pipeline.py cleanup --target-file "%~2"
    goto done
)

if "%~1"=="stats" (
    py tools\pgn_integrate.py --stats-only --dataset data\positions\quiet_positions.txt
    goto done
)

if "%~1"=="integrate" (
    if "%~2"=="" (
        echo Error: Missing input PGN path.
        echo Usage: train_pipeline.bat integrate path\to\games.pgn
        exit /b 1
    )
    py tools\pgn_integrate.py --input "%~2" --dataset data\positions\quiet_positions.txt --min-elo 2000
    goto done
)

if "%~1"=="export" (
    set OUT_FILE=%~2
    if "!OUT_FILE!"=="" set OUT_FILE=data\params\tuned_params_latest.json
    py tools\texel_fast.py --export-params "!OUT_FILE!"
    goto done
)

if "%~1"=="tune" (
    set ITERS=%~2
    if "!ITERS!"=="" set ITERS=5
    set MAX_POS=%~3
    if "!MAX_POS!"=="" set MAX_POS=100000
    py tools\texel_fast.py --positions data\positions\quiet_positions.txt --iterations !ITERS! --max-positions !MAX_POS! --output-params data\params\tuned_params_latest.json
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
    py tools\pgn_integrate.py --input "%~2" --dataset data\positions\quiet_positions.txt --min-elo 2000
    if errorlevel 1 exit /b 1
    py tools\texel_fast.py --positions data\positions\quiet_positions.txt --iterations !ITERS! --max-positions !MAX_POS! --output-params data\params\tuned_params_latest.json
    goto done
)

echo Unknown action: %~1
echo.

:help
echo ChessMinds Training Pipeline Runner
echo.
echo Usage:
echo   train_pipeline.bat status
echo   train_pipeline.bat extract [path\to\file.pgn]
echo   train_pipeline.bat train-batches [batch_size] [iterations]
echo   train_pipeline.bat resume [batch_size] [iterations]
echo   train_pipeline.bat cleanup ^<filename.pgn^>
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
