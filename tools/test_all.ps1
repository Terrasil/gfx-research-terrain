param(
    [string]$BuildDir = "cmake-build-release",
    [string]$ValidationOutput = "results/validation"
)

$ErrorActionPreference = "Stop"

cmake --build $BuildDir --target gfx-research-terrain -j 30
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& "$PSScriptRoot/run_validation.ps1" -BuildDir $BuildDir -Output $ValidationOutput -SkipBuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
