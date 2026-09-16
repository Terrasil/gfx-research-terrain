param(
    [string]$BuildDir = "cmake-build-release",
    [string]$Output = "results/doctoral",
    [switch]$Quick,
    [switch]$SaveAllCaptures,
    [switch]$SkipValidation
)

$ErrorActionPreference = "Stop"
$exe = Join-Path $BuildDir "gfx-research-terrain.exe"
if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe"
}


if (-not $SkipValidation) {
    & "$PSScriptRoot/run_validation.ps1" -BuildDir $BuildDir -Output "results/validation-pre-doctoral" -SkipBuild
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$mode = if ($Quick) { "--doctoral-quick" } else { "--doctoral-suite" }
$arguments = @($mode, "--benchmark-output=$Output")
if ($SaveAllCaptures) { $arguments += "--save-all-captures" }

& $exe @arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

python tools/analyze_results.py $Output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Doctoral results: $Output"
Write-Host "Start with doctoral_report.md, matched_quality.csv, calibration_summary.csv and silhouette.csv"
