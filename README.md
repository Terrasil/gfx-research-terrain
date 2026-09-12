# gfx-research-terrain

Minimal OpenGL 4.6 research harness for **error-controlled multiresolution terrain representation**. It is the first executable experiment for the doctoral direction *Adaptive Error-Bounded Data Representations for Real-Time Rendering*.

The project deliberately tests one narrow question before adding displacement, normal-map or material-domain migration:

> Can an explicit screen-space error estimator select terrain resolution more consistently than a conventional distance-only LOD heuristic, and can feature-aware terms improve ridge/cliff preservation at comparable cost?

The executable imports `Terrasil/gfx-research-base`; experiment-specific selection, terrain generation, benchmarks and analysis stay in this repository.

## Methods

- `reference` — finest available mesh in every patch;
- `distance-lod` — conventional distance heuristic with a swept LOD bias;
- `error-bounded` — coarsest LOD whose projected geometric residual is below the requested pixel budget;
- `error-bounded-feature` — the same controller with preregistered curvature/normal-sensitive terms.

`reference` is a finite mesh, not the analytic ground truth. `lod_error.csv` records the residual of every patch/LOD, including the finest level, against the analytic heightfield.

## Controlled surfaces

Four deterministic analytic heightfields are provided:

1. smooth hills;
2. sharp ridge;
3. mixed spatial frequencies;
4. steep cliff band.

Analytic source data lets the project measure each discrete representation independently from the rendered image.

## Crack control

Different patch LODs create T-junctions if rendered naively. The vertex shader therefore snaps the finer patch boundary to the coarser neighbour's piecewise-linear edge. Interior resolution remains unchanged. This prevents ordinary seam cracks from dominating the quality comparison.

## Build

Recommended local-base build on Windows/MinGW:

```powershell
cmake -S . -B cmake-build-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DGFX_RESEARCH_BASE_LOCAL_PATH=../gfx-research-base
cmake --build cmake-build-release --target gfx-research-terrain -j 30
```

Without `GFX_RESEARCH_BASE_LOCAL_PATH`, the project fetches `Terrasil/gfx-research-base` through CPM.

## Interactive run

```powershell
.\cmake-build-release\gfx-research-terrain.exe
```

The GUI exposes method, scene, pixel-error budget, feature weight, hysteresis, distance-LOD bias, camera position, wireframe and LOD visualization.

## Quick research validation

```powershell
.\cmake-build-release\gfx-research-terrain.exe `
  --publication-quick `
  --benchmark-output=results/quick

python tools/analyze_results.py results/quick
```

This intentionally small sweep checks the pipeline after code or shader changes.

## Full publication sweep

```powershell
.\cmake-build-release\gfx-research-terrain.exe `
  --publication-suite `
  --benchmark-output=results/publication

python tools/analyze_results.py results/publication
```

The suite contains:

- primary quality/cost curves;
- distance-baseline bias sweep;
- error-budget sweep;
- feature-aware ablation;
- resolution scaling;
- temporal camera motion with/without hysteresis;
- forced-finest null/equivalence cases.

## Output

A run writes:

- `manifest.txt` — repository/build/GPU/OpenGL and experiment parameters;
- `lod_error.csv` — offline geometric/normal/feature error of every LOD;
- `raw.csv` — per-frame GPU time, triangle count, transitions and selected-LOD histogram;
- `quality.csv` — HDR image error, foreground error, depth error and exact-equality flags;
- `summary.csv` — aggregated timing/work statistics;
- `cases.csv` — one row per benchmark case with timing joined to quality;
- `pareto.csv` — primary quality/cost Pareto classification;
- `temporal.csv` — temporal transition summary;
- `null.csv` — null-test pass/fail summary;
- selected `captures/*.png`.

No result is considered evidence until it comes from a frozen Release revision and the manifest is archived with the raw CSV files.

## Useful CLI overrides

```text
--error-budget=<pixels>
--feature-weight=<value>
--hysteresis=<value>
--distance-bias=<integer>
--patches=<count-per-side>
--max-cells=<cells-per-patch>
--lod-count=<count>
--error-samples=<samples-per-side>
--warmup=<frames>
--samples=<frames>
--save-all-captures
--visualize-lod
--wireframe
```

See `PUBLICATION_TESTS.md` for the frozen hypotheses and `THEORY.md` for the exact estimator used by the implementation.
