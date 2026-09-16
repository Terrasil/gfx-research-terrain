param(
    [string]$BuildDir = "cmake-build-release",
    [string]$Output = "results/thesis-figures-pbr-v6.2",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root $BuildDir
$exe = Join-Path $exe "gfx-research-terrain.exe"

if (-not $SkipBuild) {
    cmake --build (Join-Path $root $BuildDir) --config Release --target gfx-research-terrain -j 8
}

if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe"
}

& "$PSScriptRoot/run_validation.ps1" -BuildDir $BuildDir -Output "results/validation-pre-thesis-figures-pbr" -SkipBuild
& $exe --thesis-figures-pbr --realistic --smooth-normals "--benchmark-output=$Output"

$captureDir = Join-Path $root $Output
$captureDir = Join-Path $captureDir "captures"
$figureOutput = Join-Path $root $Output
$figureOutput = Join-Path $figureOutput "thesis-figures"
New-Item -ItemType Directory -Force -Path $figureOutput | Out-Null

python (Join-Path $PSScriptRoot "make_thesis_figures.py") --captures $captureDir --output $figureOutput
Write-Host "PBR thesis figures ready: $figureOutput"
