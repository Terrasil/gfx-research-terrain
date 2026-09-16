param(
    [string]$Python = "python",
    [string]$Source = ""
)

$ErrorActionPreference = "Stop"
$script = Join-Path $PSScriptRoot "setup_mount_st_helens_dem.py"

Write-Host "Preparing USGS 3DEP 1/3 arc-second Mount St. Helens heightmap..."
Write-Host "Target: 1921x1921 samples, 10 m spacing, 19.2 km square."

& $Python -c "import numpy, rasterio" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Installing Python dependencies (numpy, rasterio)..."
    & $Python -m pip install --user numpy rasterio
    if ($LASTEXITCODE -ne 0) { throw "Could not install numpy/rasterio." }
}

if ($Source -ne "") {
    & $Python $script --source $Source
} else {
    & $Python $script
}
if ($LASTEXITCODE -ne 0) { throw "DEM preparation failed." }

Write-Host "USGS 10 m DEM ready. Rebuild/restart the application if it was already open."
