<#
.SYNOPSIS
    ChessMinds Incremental Training & Tuning Pipeline
.DESCRIPTION
    Automates dataset integration (append-only, deduplication) and Texel evaluation tuning.
    Never overwrites or deletes existing training data or tuned weights.

.PARAMETER Action
    The action to perform:
      - 'integrate' : Extract quiet positions from new PGN and append to dataset (with deduplication)
      - 'stats'     : Show dataset position counts, win/draw/loss distribution, and size
      - 'tune'      : Run Fast Texel coordinate descent tuning initialized from existing weights
      - 'full'      : Run integrate followed by tune (end-to-end pipeline)
      - 'export'    : Export current eval parameters from src/eval.cpp to a JSON file

.PARAMETER InputPgn
    Path to new PGN file or directory containing PGN files (default: data/raw/lichess_db_standard_rated_2014-02.pgn)

.PARAMETER Dataset
    Path to quiet positions dataset (default: data/positions/quiet_positions.txt)

.PARAMETER MinElo
    Minimum Elo rating for both players (default: 2000; set 0 for no filter)

.PARAMETER SkipOpening
    Full moves to skip at the start of each game (default: 8)

.PARAMETER MaxNewPositions
    Max positions to extract from new PGN (default: 500,000)

.PARAMETER MaxTrainPositions
    Max positions to use during Texel tuning (default: 100,000)

.PARAMETER Iterations
    Number of coordinate descent iterations (default: 5)

.PARAMETER StepSize
    Adjustment step size in centipawns (default: 5)

.PARAMETER ParamsFile
    Optional JSON file to initialize tuning parameters from (default: current src/eval.cpp)

.PARAMETER OutputParams
    Path to save final tuned parameters JSON (default: data/params/tuned_params_latest.json)

.PARAMETER NoRebuild
    Skip rebuilding the C++ engine after tuning

.EXAMPLE
    # Show dataset statistics
    .\train_pipeline.ps1 -Action stats

    # Integrate a new PGN file (append-only, deduplicated)
    .\train_pipeline.ps1 -Action integrate -InputPgn "data/raw/lichess_db_standard_rated_2014-02.pgn"

    # Incremental tuning starting from current tuned engine parameters
    .\train_pipeline.ps1 -Action tune -Iterations 5 -MaxTrainPositions 100000

    # Full end-to-end pipeline
    .\train_pipeline.ps1 -Action full -InputPgn "data/raw/lichess_db_standard_rated_2014-02.pgn" -Iterations 5
#>

[CmdletBinding()]
param(
    [ValidateSet("integrate", "stats", "tune", "full", "export")]
    [string]$Action = "stats",

    [string]$InputPgn = "data/raw/lichess_db_standard_rated_2014-02.pgn",
    [string]$Dataset = "data/positions/quiet_positions.txt",
    [int]$MinElo = 2000,
    [int]$SkipOpening = 8,
    [int]$MaxNewPositions = 500000,
    [int]$MaxTrainPositions = 100000,
    [int]$Iterations = 5,
    [int]$StepSize = 5,
    [string]$ParamsFile = "",
    [string]$OutputParams = "data/params/tuned_params_latest.json",
    [switch]$NoRebuild
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "   ChessMinds Incremental Training & Tuning Pipeline" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Action: $Action" -ForegroundColor Yellow
Write-Host "  Date  : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Gray
Write-Host "============================================================" -ForegroundColor Cyan

function Run-DatasetStats {
    Write-Host "`n>>> Running Dataset Statistics..." -ForegroundColor Green
    py tools/pgn_integrate.py --stats-only --dataset $Dataset
    if ($LASTEXITCODE -ne 0) { throw "Dataset statistics check failed." }
}

function Run-Integration {
    Write-Host "`n>>> Integrating New PGN Data (Append-Only, Deduplicated)..." -ForegroundColor Green
    Write-Host "  Input PGN : $InputPgn"
    Write-Host "  Dataset   : $Dataset"
    Write-Host "  Min Elo   : $MinElo"
    Write-Host "  Max Pos   : $MaxNewPositions"

    $argsList = @(
        "tools/pgn_integrate.py",
        "--input", $InputPgn,
        "--dataset", $Dataset,
        "--min-elo", $MinElo,
        "--skip-opening", $SkipOpening,
        "--max-positions", $MaxNewPositions
    )
    py @argsList
    if ($LASTEXITCODE -ne 0) { throw "PGN integration failed." }
}

function Run-Export {
    Write-Host "`n>>> Exporting Parameters to JSON..." -ForegroundColor Green
    $argsList = @(
        "tools/texel_fast.py",
        "--export-params", $OutputParams
    )
    if ($ParamsFile -ne "") {
        $argsList += @("--params-file", $ParamsFile)
    }
    py @argsList
    if ($LASTEXITCODE -ne 0) { throw "Parameter export failed." }
}

function Run-Tuning {
    Write-Host "`n>>> Running Texel Coordinate Descent Tuning..." -ForegroundColor Green
    Write-Host "  Positions      : $Dataset"
    Write-Host "  Max Train Pos  : $MaxTrainPositions"
    Write-Host "  Iterations     : $Iterations"
    Write-Host "  Step Size      : $StepSize cp"

    $argsList = @(
        "tools/texel_fast.py",
        "--positions", $Dataset,
        "--max-positions", $MaxTrainPositions,
        "--iterations", $Iterations,
        "--step", $StepSize,
        "--output-params", $OutputParams
    )
    if ($ParamsFile -ne "") {
        $argsList += @("--params-file", $ParamsFile)
    }
    if ($NoRebuild) {
        $argsList += "--no-rebuild"
    }
    py @argsList
    if ($LASTEXITCODE -ne 0) { throw "Texel tuning failed." }
}

switch ($Action) {
    "stats" {
        Run-DatasetStats
    }
    "integrate" {
        Run-Integration
    }
    "export" {
        Run-Export
    }
    "tune" {
        Run-Tuning
    }
    "full" {
        Run-Integration
        Run-Tuning
    }
}

Write-Host "`n[✓] Pipeline action '$Action' completed successfully." -ForegroundColor Green
