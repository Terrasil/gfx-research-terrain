# gfx-research-terrain

Experimental C++ and OpenGL 4.6 platform used to evaluate adaptive heightfield tessellation with explicit error control for real-time rendering.

The repository contains the implementation used for the dissertation experiments, automated validation, benchmark scripts, figure generation tools, PBR terrain materials and the prepared Mount St. Helens DEM used for supplementary validation.

## Scope

The main evaluated methods are:

- `tessellation-distance`: distance-based LOD reference method
- `tessellation-variance-baseline`: RMS reconstruction residual reference method
- `tessellation-error`: maximum projected reconstruction residual
- `tessellation-normal-bound`: projected residual with an independent normal-angle constraint
- `tessellation-context-aware`: projected residual with local shading-response sensitivity

The experimentally validated scope is regular heightfields rendered with GPU tessellation. The project does not treat GPU tessellation, screen-space error or error-bounded LOD themselves as new techniques.

## Requirements

- CMake 3.25 or newer
- Ninja
- C++20 compiler
- OpenGL 4.6 capable GPU and driver
- Python 3 for analysis and validation scripts

The build obtains `gfx-research-base` automatically through CPM unless a local checkout is supplied.

## Build

```powershell
cmake -S . -B cmake-build-release -G Ninja `
    -DCMAKE_BUILD_TYPE=Release

cmake --build cmake-build-release --target gfx-research-terrain -j 30
```

A local base checkout can be used with:

```powershell
cmake -S . -B cmake-build-release -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DGFX_RESEARCH_BASE_LOCAL_PATH=E:/gfx-research-base
```

## Interactive run

```powershell
.\cmake-build-release\gfx-research-terrain.exe
```

The GUI exposes scientific and realistic PBR views, scene selection, error budgets, normal and radiometric constraints, tessellation settings, camera controls and automated research suites.

## Validation

Run the complete validation pipeline before collecting research results:

```powershell
.\tools\test_all.ps1
```

Static and contract tests can also be executed directly:

```powershell
python tools/run_unit_tests.py
```

The validation suite checks, among other properties, null equivalence, topology, error-model behavior, GPU method coverage and benchmark-output consistency.

## Main research runs

Quick doctoral sweep:

```powershell
.\tools\run_doctoral.ps1 -Quick
```

Full doctoral sweep:

```powershell
.\tools\run_doctoral.ps1
```

Real DEM validation:

```powershell
.\tools\run_real_dem_validation.ps1
```

Dissertation figure captures:

```powershell
.\tools\run_thesis_figures.ps1
.\tools\run_thesis_figures_pbr.ps1
```

Generated benchmark data are written under `results/` and are intentionally excluded from Git. Raw CSV files should be archived together with the exact Git commit used for a published or dissertation result.

## Mount St. Helens DEM

The supplementary measured-terrain test uses a USGS 3DEP 1/3 arc-second DEM.

Frozen source:

- dataset: USGS 3DEP 1/3 arc-second DEM
- source tile: `USGS_13_n47w123_20250813.tif`
- source dimensions: 10812 x 10812 float32
- source CRS: EPSG:4269
- source SHA-256: `ab4fb0a2afa65c49c41cb0c59c5ea102329b6e1e47425159b884d05f540c4bb5`

Prepared runtime asset:

- file: `assets/dem/mount_st_helens_usgs_10m_1921_f32.raw`
- dimensions: 1921 x 1921
- sample spacing: 10 m
- working CRS: EPSG:26910
- physical span: 19.2 km x 19.2 km
- output SHA-256: `f99242f2fc0fdc9977b4ba308b5d0f7164c47dbce2b9d4ebabac61c4c6c764b1`

The C++ application does not decode GeoTIFF at runtime. It reads the prepared float32 RAW file, verifies the expected size and checks that every sample is finite before upload to OpenGL.

The prepared asset can be regenerated from the original GeoTIFF with:

```powershell
.\tools\setup_mount_st_helens_dem.ps1 -Source "C:\path\to\USGS_13_n47w123_20250813.tif"
```

Detailed source and transformation metadata are stored in `assets/dem/mount_st_helens_usgs_10m_1921_metadata.json`.

## PBR materials

The grass, dirt, rock and sand PBR textures under `assets/terrain/` originate from Poly Haven:

`https://polyhaven.com/`

Poly Haven assets are distributed under CC0. These textures are used by the realistic visualization path. Quantitative scientific comparisons use the controlled scientific shading path unless a test explicitly states otherwise.

## Repository layout

```text
assets/       runtime DEM and terrain materials
cmake/        CMake helper files
include/      project headers
shaders/      OpenGL shader sources
src/          C++ implementation
tests/        static and contract tests
tools/        benchmark, validation, DEM preparation and analysis scripts
```

Local dependency checkouts, virtual environments, IDE files, build directories, raw source GeoTIFF files and generated benchmark results are excluded by `.gitignore`.
