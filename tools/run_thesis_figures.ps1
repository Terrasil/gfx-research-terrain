param(
    [string]$BuildDir = "cmake-build-release",
    [string]$Output = "results/thesis-figures-v6.2",
    [string]$BasePath = "../gfx-research-base",
    [switch]$SkipBuild,
    [switch]$SkipValidation
)

$ErrorActionPreference = "Stop"

if (-not $SkipBuild) {
    $configureArgs = @("-S", ".", "-B", $BuildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
    if (Test-Path $BasePath) {
        $resolvedBase = (Resolve-Path $BasePath).Path
        $configureArgs += "-DGFX_RESEARCH_BASE_LOCAL_PATH=$resolvedBase"
    }
    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build $BuildDir --target gfx-research-terrain -j 30
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$exe = Join-Path $BuildDir "gfx-research-terrain.exe"
if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe"
}

python tools/run_unit_tests.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $SkipValidation) {
    & "$PSScriptRoot/run_validation.ps1" -BuildDir $BuildDir -Output "results/validation-pre-thesis-figures" -SkipBuild
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if (Test-Path $Output) {
    Remove-Item -Recurse -Force $Output
}

& $exe --thesis-figures --scientific "--benchmark-output=$Output"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

python tools/analyze_results.py $Output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$figureOutput = Join-Path $Output "thesis-figures"
python tools/make_thesis_figures.py $Output $figureOutput
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Thesis visual suite complete."
Write-Host "Raw screenshots: $(Join-Path $Output 'captures')"
Write-Host "Publication figures: $figureOutput"
Write-Host "Quantitative CSV files remain in: $Output"
