param(
    [string]$BuildDir = "cmake-build-release",
    [string]$Output = "results/validation",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

if (-not $SkipBuild) {
    cmake --build $BuildDir --target gfx-research-terrain -j 30
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

python tools/run_unit_tests.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $BuildDir "gfx-research-terrain.exe"
if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe"
}

if (Test-Path $Output) {
    Remove-Item -Recurse -Force $Output
}

& $exe --validation-suite "--benchmark-output=$Output"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

python tools/analyze_results.py $Output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

python tools/validate_results.py $Output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "AUTOMATED VALIDATION PASSED" -ForegroundColor Green
Write-Host "Report: $Output/validation_report.md"
