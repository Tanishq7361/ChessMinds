<#
.SYNOPSIS
    ChessMinds Incremental Training, Queue & Sequential Batch Pipeline
.DESCRIPTION
    Automates dataset integration, queue extraction, reproducible batch slicing (100K positions),
    validation scoring, crash recovery, and Texel evaluation tuning.
    Never overwrites or deletes existing training data or tuned weights.

.PARAMETER Action
    The action to perform:
      - 'status'        : Display pending, processed, and validation counts, plus disk usage
      - 'extract'       : Stream raw PGN games into pending training queue & validation dataset
      - 'train-batches' : Train sequentially using 100,000-position batches with parameter continuity
      - 'resume'        : Resume an interrupted batch training loop safely from the latest checkpoint
      - 'cleanup'       : Delete a raw PGN file ONLY after verified extraction and explicit confirmation
      - 'stats'         : Show legacy dataset position counts, win/draw/loss distribution, and size
      - 'integrate'     : Extract quiet positions from new PGN and append to dataset (with deduplication)
      - 'tune'          : Run single-run Fast Texel coordinate descent tuning
      - 'full'          : Run integrate followed by tune (end-to-end pipeline)
      - 'export'        : Export current eval parameters from src/eval.cpp to a JSON file

.PARAMETER InputPgn
    Path to raw PGN file or directory containing PGN files (default: data/raw)

.PARAMETER BatchSize
    Number of positions per training batch (default: 100,000)

.PARAMETER MaxBatches
    Max batches to run in this invocation (default: $null / run until queue empty)

.PARAMETER Iterations
    Number of coordinate descent iterations per batch/run (default: 3 for batches, 5 for tune)

.PARAMETER StepSize
    Adjustment step size in centipawns (default: 5)

.PARAMETER MinElo
    Minimum Elo rating for both players (default: 2000; set 0 for no filter)

.PARAMETER SkipOpening
    Full moves to skip at the start of each game (default: 8)

.PARAMETER MaxNewPositions
    Max positions to extract from new PGN (default: 500,000)

.PARAMETER Seed
    Random seed for queue shuffling and reproducibility (default: 42)

.PARAMETER TargetFile
    Specific raw PGN filename to delete during 'cleanup' (e.g. lichess_db_standard_rated_2014-02.pgn)

.PARAMETER Force
    Bypass interactive confirmation during 'cleanup'

.PARAMETER NoRebuild
    Skip rebuilding the C++ engine after tuning
#>

[CmdletBinding()]
param(
    [ValidateSet("status", "extract", "train-batches", "resume", "cleanup", "stats", "integrate", "tune", "full", "export")]
    [string]$Action = "status",

    [string]$InputPgn = "data/raw",
    [string]$Dataset = "data/positions/quiet_positions.txt",
    [int]$BatchSize = 100000,
    [Nullable[int]]$MaxBatches = $null,
    [int]$MinElo = 2000,
    [int]$SkipOpening = 8,
    [int]$MaxNewPositions = 500000,
    [int]$MaxTrainPositions = 100000,
    [int]$Iterations = 3,
    [int]$StepSize = 5,
    [int]$Seed = 42,
    [string]$ParamsFile = "",
    [string]$OutputParams = "data/params/tuned_params_latest.json",
    [string]$TargetFile = "",
    [switch]$Force,
    [switch]$NoRebuild
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "   ChessMinds Training, Queue & Sequential Batch Pipeline   " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Action: $Action" -ForegroundColor Yellow
Write-Host "  Date  : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Gray
Write-Host "============================================================" -ForegroundColor Cyan

function Run-BatchStatus {
    py tools/batch_pipeline.py status
    if ($LASTEXITCODE -ne 0) { throw "Status command failed." }
}

function Run-BatchExtract {
    Write-Host "`n>>> Extracting Raw PGN Files into Training Queue..." -ForegroundColor Green
    $argsList = @(
        "tools/batch_pipeline.py",
        "extract",
        "--input", $InputPgn,
        "--min-elo", $MinElo,
        "--skip-opening", $SkipOpening,
        "--seed", $Seed
    )
    if ($PSBoundParameters.ContainsKey("MaxNewPositions")) {
        $argsList += @("--max-positions", $MaxNewPositions)
    }
    py @argsList
    if ($LASTEXITCODE -ne 0) { throw "PGN extraction to queue failed." }
}

function Run-BatchTraining {
    Write-Host "`n>>> Running Sequential 100K-Position Batch Training..." -ForegroundColor Green
    $argsList = @(
        "tools/batch_pipeline.py",
        "train-batches",
        "--batch-size", $BatchSize,
        "--iterations", $Iterations,
        "--step", $StepSize
    )
    if ($MaxBatches -ne $null) {
        $argsList += @("--max-batches", $MaxBatches)
    }
    if ($NoRebuild) {
        $argsList += "--no-rebuild"
    }
    py @argsList
    if ($LASTEXITCODE -ne 0) { throw "Batch training sequence failed." }
}

function Run-BatchResume {
    Write-Host "`n>>> Resuming Interrupted Batch Training Pipeline..." -ForegroundColor Green
    $argsList = @(
        "tools/batch_pipeline.py",
        "resume",
        "--batch-size", $BatchSize,
        "--iterations", $Iterations,
        "--step", $StepSize
    )
    if ($MaxBatches -ne $null) {
        $argsList += @("--max-batches", $MaxBatches)
    }
    if ($NoRebuild) {
        $argsList += "--no-rebuild"
    }
    py @argsList
    if ($LASTEXITCODE -ne 0) { throw "Resume batch training failed." }
}

function Run-BatchCleanup {
    Write-Host "`n>>> Performing Verified Raw PGN File Cleanup..." -ForegroundColor Green
    if ($TargetFile -eq "") {
        throw "TargetFile parameter must be specified for cleanup (e.g. -TargetFile lichess_db_standard_rated_2014-02.pgn)"
    }
    $argsList = @(
        "tools/batch_pipeline.py",
        "cleanup",
        "--target-file", $TargetFile
    )
    if ($Force) {
        $argsList += "--force"
    }
    py @argsList
    if ($LASTEXITCODE -ne 0) { throw "PGN cleanup failed or was cancelled." }
}

# ─── Legacy Actions (Preserved for Backward Compatibility) ────────────────────

function Run-DatasetStats {
    Write-Host "`n>>> Running Dataset Statistics..." -ForegroundColor Green
    py tools/pgn_integrate.py --stats-only --dataset $Dataset
    if ($LASTEXITCODE -ne 0) { throw "Dataset statistics check failed." }
}

function Run-Integration {
    Write-Host "`n>>> Integrating New PGN Data (Append-Only, Deduplicated)..." -ForegroundColor Green
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
    Write-Host "`n>>> Running Single-Run Texel Coordinate Descent Tuning..." -ForegroundColor Green
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
    "status"        { Run-BatchStatus }
    "extract"       { Run-BatchExtract }
    "train-batches" { Run-BatchTraining }
    "resume"        { Run-BatchResume }
    "cleanup"       { Run-BatchCleanup }
    "stats"         { Run-DatasetStats }
    "integrate"     { Run-Integration }
    "export"        { Run-Export }
    "tune"          { Run-Tuning }
    "full"          {
        Run-Integration
        Run-Tuning
    }
}

Write-Host "`n[✓] Pipeline action '$Action' completed successfully." -ForegroundColor Green
