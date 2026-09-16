param(
    [string]$BuildDir = "cmake-build-release",
    [string]$Output = "results/real-dem-validation",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$buildPath = Join-Path $root $BuildDir
$exe = Join-Path $buildPath "gfx-research-terrain.exe"

if (-not $SkipBuild) {
    cmake --build $buildPath --config Release --target gfx-research-terrain -j 8
}

if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe"
}

Push-Location $root
try {
    & $exe --real-dem-validation "--benchmark-output=$Output"
    if ($LASTEXITCODE -ne 0) {
        throw "Real DEM benchmark failed with exit code $LASTEXITCODE"
    }
    python (Join-Path $PSScriptRoot "analyze_real_dem.py") (Join-Path $root $Output)
    if ($LASTEXITCODE -ne 0) {
        throw "Real DEM analysis failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

Write-Host "Real DEM validation ready: $(Join-Path $root $Output)"
