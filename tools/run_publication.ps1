param(
    [string]$BuildDir = "cmake-build-release",
    [string]$Output = "results/publication",
    [switch]$Quick
)

$ErrorActionPreference = "Stop"
$exe = Join-Path $BuildDir "gfx-research-terrain.exe"
if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe"
}

$mode = if ($Quick) { "--publication-quick" } else { "--publication-suite" }
& $exe $mode "--benchmark-output=$Output"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

python tools/analyze_results.py $Output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Results: $Output"
