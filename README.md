# gfx-research-terrain

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)
[![OpenGL 4.6](https://img.shields.io/badge/OpenGL-4.6-5586A4.svg)](https://www.opengl.org/)
[![CMake](https://img.shields.io/badge/CMake-3.25%2B-064F8C.svg)](https://cmake.org/)

An adaptive heightfield tessellation laboratory with projected-error controls and a separate Mount St. Helens feature-map study.

[Method](#method) · [Build](#build) · [Controls](#controls) · [Experiments](#experiments) · [References](#references)

## Method

Let $\varepsilon_e$ be the maximum sampled height residual along an edge in world units. With focal length $f_y$ in pixels, camera distance $d$ and pixel budget $\tau$, the geometric controller uses $g=f_y\varepsilon_e/(d\tau)$. The feature-aware variant applies a modulation before choosing an edge tessellation factor.

```math
\begin{aligned}
F(x)&=\operatorname{clamp}\left(
 \frac{\max_{q\in\mathcal N_4(x)}\arccos(\mathbf n_x\cdot\mathbf n_q)}{25^\circ},
 0,1\right),\\
F_e&=\max\{F(a),F((a+b)/2),F(b)\},\\
q_e&=\operatorname{clamp}\left(1.15\sqrt{g(1+\lambda F_e)},\,1,\,q_{\max}\right).
\end{aligned}
```

$F$ is computed from the bundled 10 m DEM and stored as R32F. Both patches sharing an edge use the same world-space sample positions. The estimated projected error uses sampled residuals, and the feature texture represents local normal variation. The offline experiment is supplementary to the doctoral projected-residual analysis.

## Build

Requirements are CMake 3.25+, a C++20 compiler, Ninja, Python 3 with Jinja2, and a desktop OpenGL 4.6 Core driver. The initial CMake configuration downloads the pinned gfx-research-base revision and its dependencies. A local checkout can be supplied with `-DGFX_RESEARCH_BASE_LOCAL_PATH=../gfx-research-base`.

```powershell
python -m pip install jinja2 numpy
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
```

On Linux, run the same build commands and omit `.exe` when launching an executable.

## Controls

Launch `gfx-research-terrain.exe` for the adaptive terrain viewer. The Method panel exposes the feature-map toggle on Mount St. Helens. The reference is the finest available tessellation.

**Tests** starts the offline feature smoke test, the full offline feature comparison or one of the thesis suites. Each GUI offline-feature run writes to a new folder under `results`.

## Experiments

```powershell
.\out\build\gfx-research-terrain.exe --offline-feature-smoke --benchmark-output=results/dem-smoke
python tools/analyze_offline_features.py results/dem-smoke
.\out\build\gfx-research-terrain.exe --offline-feature-study --warmup=20 --samples=60 --benchmark-output=results/dem-paper
python tools/analyze_offline_features.py results/dem-paper
```

The smoke profile uses reduced tessellation. The full feature study measures 72 cases across fixed viewpoints and paired dolly trajectories at 1280 × 720. The controls are runtime feature sampling, the projected-residual path, and a distance-based path. Choose `--doctoral-suite` or use `tools/run_doctoral.ps1` for the existing thesis protocol.

| Output | Contents |
|---|---|
| `raw.csv` | Synchronized per-frame GPU draw time, primitive count and camera trajectory |
| `quality.csv` | End-view HDR, depth and silhouette comparisons to the dense reference |
| `offline_feature_pairs.csv` | Paired timing, triangle-count variation, image loss and silhouette loss |
| `offline_feature_inputs.json` | Verified hashes of the DEM and the feature texture |

The motion metric is normalized frame-to-frame change in triangle count. The paper scope of this ablation is one real DEM with multiple matched trajectories.

## References

1. Peter Lindstrom and Valerio Pascucci. [Terrain Simplification Simplified](https://www.pascucci.org/pdf-papers/IEEE-TVG-2002.pdf). IEEE TVCG, 2002.
2. Frank Losasso and Hugues Hoppe. [Geometry Clipmaps](https://hhoppe.com/proj/geomclipmap/). SIGGRAPH, 2004.
3. USGS. [3DEP source data](https://data.usgs.gov/datacatalog/data/USGS:3a81321b-c153-416f-98b7-cc8e5f0e17c3). Prepared input and provenance are under `assets/dem/`.
