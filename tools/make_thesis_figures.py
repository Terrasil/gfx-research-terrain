from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch
from matplotlib.colors import LinearSegmentedColormap, ListedColormap, BoundaryNorm
from PIL import Image


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def f(row: dict[str, str], key: str, default: float = math.nan) -> float:
    try:
        return float(row.get(key, ""))
    except (TypeError, ValueError):
        return default


def save_figure(fig: plt.Figure, out_dir: Path, name: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_dir / f"{name}.pdf", bbox_inches="tight")
    fig.savefig(out_dir / f"{name}.png", dpi=220, bbox_inches="tight")
    plt.close(fig)


def find_one(capture_dir: Path, pattern: str) -> Path | None:
    matches = sorted(capture_dir.glob(pattern))
    return matches[0] if matches else None


def trim_alpha(image: Image.Image) -> Image.Image:
    if image.mode != "RGBA":
        image = image.convert("RGBA")
    alpha = image.getchannel("A")
    box = alpha.getbbox()
    return image.crop(box) if box else image


def image_panel(paths: list[tuple[str, Path]], out_dir: Path, name: str, columns: int = 2) -> None:
    valid = [(title, path) for title, path in paths if path.exists()]
    if not valid:
        return
    rows = math.ceil(len(valid) / columns)
    fig, axes = plt.subplots(rows, columns, figsize=(7.2, rows * 2.5), squeeze=False)
    for ax in axes.flat:
        ax.axis("off")
    for ax, (title, path) in zip(axes.flat, valid):
        img = Image.open(path).convert("RGB")
        ax.imshow(img)
        ax.set_title(title, fontsize=9)
        ax.axis("off")
    fig.tight_layout(pad=0.5)
    save_figure(fig, out_dir, name)


ERROR_CMAP = LinearSegmentedColormap.from_list(
    "thesis_error",
    [
        (0.004, 0.000, 0.015),
        (0.190, 0.055, 0.365),
        (0.550, 0.160, 0.505),
        (0.870, 0.290, 0.410),
        (0.995, 0.625, 0.425),
        (0.990, 0.990, 0.750),
    ],
)

LOD_COLORS = [
    (0.84, 0.10, 0.10), (0.95, 0.33, 0.08), (0.96, 0.66, 0.05),
    (0.70, 0.82, 0.10), (0.20, 0.76, 0.26), (0.08, 0.70, 0.66),
    (0.10, 0.45, 0.92), (0.36, 0.25, 0.92), (0.67, 0.18, 0.85),
    (0.92, 0.12, 0.62),
]


def raw_error_comparison(reference: Path, method: Path, error: Path, out_dir: Path, name: str) -> None:
    """Arrange three raw test artifacts; the error image is produced by the renderer."""
    fig, axes = plt.subplots(1, 3, figsize=(9.4, 3.45), constrained_layout=True)
    titles = ["Obraz odniesienia", "Metoda badana", "Lokalny błąd RGB"]
    for ax, title, path in zip(axes, titles, [reference, method, error]):
        ax.imshow(Image.open(path).convert("RGB"))
        ax.set_title(title, fontsize=10)
        ax.axis("off")
    sm = plt.cm.ScalarMappable(cmap=ERROR_CMAP, norm=plt.Normalize(0.0, 0.16))
    sm.set_array([])
    cbar = fig.colorbar(sm, ax=axes[2], fraction=0.046, pad=0.025)
    cbar.set_label("RMSE kanałów RGB, liniowe HDR", fontsize=8)
    cbar.ax.tick_params(labelsize=7)
    save_figure(fig, out_dir, name)


def lod_heatmap_figure(capture_dir: Path, out_dir: Path) -> None:
    path = find_one(capture_dir, "thesis-figure-glacial-valley-tessellation-error-*-lod.png")
    if path is None:
        return
    fig, ax = plt.subplots(figsize=(7.2, 4.8), constrained_layout=True)
    ax.imshow(Image.open(path).convert("RGB"))
    ax.axis("off")

    # For the thesis capture suite uMaxTessLevel = 16, while tcLevel stores
    # log2(maximum tessellation factor). Therefore the meaningful discrete classes
    # are 0..4, not the 0..9 levels used by the separate hierarchical mesh path.
    tess_colors = LOD_COLORS[:5]
    cmap = ListedColormap(tess_colors)
    norm = BoundaryNorm(np.arange(-0.5, 5.5, 1.0), cmap.N)
    sm = plt.cm.ScalarMappable(cmap=cmap, norm=norm)
    sm.set_array([])
    cbar = fig.colorbar(sm, ax=ax, orientation="horizontal", fraction=0.06, pad=0.035, ticks=np.arange(5))
    cbar.set_label(r"Klasa $\log_2(t_{max})$ lokalnego czynnika teselacji", fontsize=8)
    cbar.ax.tick_params(labelsize=7)
    save_figure(fig, out_dir, "tessellation_intensity_glacial")

def scene_gallery(capture_dir: Path, out_dir: Path) -> None:
    # Use only dedicated thesis overview captures. Stress-test views are intentionally
    # excluded because a grazing camera can make a valid terrain almost unreadable.
    controlled_specs = [
        ("Smooth Hills", "thesis-figure-smooth-hills-tessellation-error-*-reference.png"),
        ("Sharp Ridge", "thesis-figure-sharp-ridge-tessellation-error-*-reference.png"),
        ("Mixed Frequency", "thesis-figure-mixed-frequency-tessellation-error-*-reference.png"),
        ("Cliff Band", "thesis-figure-cliff-band-tessellation-error-*-reference.png"),
    ]
    natural_specs = [
        ("Alpine Escarpment", "thesis-figure-alpine-escarpment-tessellation-error-*-reference.png"),
        ("Glacial Valley", "thesis-figure-glacial-valley-tessellation-error-*-reference.png"),
        ("Badlands", "thesis-figure-badlands-tessellation-error-*-reference.png"),
        ("Fractured Highlands", "thesis-figure-fractured-highlands-tessellation-error-*-reference.png"),
    ]

    def build(specs: list[tuple[str, str]], name: str, columns: int) -> None:
        paths: list[tuple[str, Path]] = []
        for title, pattern in specs:
            path = find_one(capture_dir, pattern)
            if path:
                paths.append((title, path))
        image_panel(paths, out_dir, name, columns=columns)

    build(controlled_specs, "scene_gallery_controlled", columns=1)
    build(natural_specs, "scene_gallery_natural", columns=1)

def camera_candidates(capture_dir: Path, out_dir: Path, scene_slug: str, output_name: str, columns: int = 2) -> None:
    candidates = sorted(capture_dir.glob(f"thesis-camera-{scene_slug}-tessellation-error-*-reference.png"))
    if not candidates:
        return
    specs: list[tuple[str, Path]] = []
    import re
    for path in candidates:
        match = re.search(r"-yaw(-?\d+)-", path.name)
        label = f"azymut {match.group(1)}°" if match else path.stem
        specs.append((label, path))
    image_panel(specs, out_dir, output_name, columns=columns)


def alpine_camera_candidates(capture_dir: Path, out_dir: Path) -> None:
    camera_candidates(capture_dir, out_dir, "alpine-escarpment", "alpine_camera_candidates", columns=2)


def cliffband_camera_candidates(capture_dir: Path, out_dir: Path) -> None:
    camera_candidates(capture_dir, out_dir, "cliff-band", "cliffband_camera_candidates", columns=3)


def single_scene_overviews(capture_dir: Path, out_dir: Path) -> None:
    specs = [
        ("cliff_band_overview", "thesis-figure-cliff-band-tessellation-error-*-reference.png"),
        ("alpine_escarpment_overview", "thesis-figure-alpine-escarpment-tessellation-error-*-reference.png"),
        ("glacial_valley_overview", "thesis-figure-glacial-valley-tessellation-error-*-reference.png"),
    ]
    for name, pattern in specs:
        path = find_one(capture_dir, pattern)
        if path is None:
            continue
        fig, ax = plt.subplots(figsize=(7.2, 4.2), constrained_layout=True)
        ax.imshow(Image.open(path).convert("RGB"))
        ax.axis("off")
        save_figure(fig, out_dir, name)


def diagnostic_gallery(capture_dir: Path, out_dir: Path) -> None:
    # These files are produced by the thesis capture mode. The panel is generated only
    # when all requested diagnostics exist, so older result packages remain compatible.
    base = find_one(capture_dir, "thesis-figure-glacial-valley-tessellation-error-*-method.png")
    if base is None:
        return
    stem = base.name.removesuffix("-method.png")
    specs = [
        ("Render poglądowy (GGX)", capture_dir / f"{stem}-realistic.png"),
        ("Klasa czynnika teselacji", capture_dir / f"{stem}-lod.png"),
        ("Błąd projekcyjny / próg", capture_dir / f"{stem}-error.png"),
        ("Zmienność kierunku normalnej", capture_dir / f"{stem}-normal.png"),
        ("Odległość od progu błędu", capture_dir / f"{stem}-margin.png"),
        ("Nachylenie powierzchni", capture_dir / f"{stem}-slope.png"),
    ]
    if sum(path.exists() for _, path in specs) >= 4:
        image_panel(specs, out_dir, "diagnostic_heatmaps", columns=2)


def shading_normal_gallery_for_scene(capture_dir: Path, out_dir: Path, scene_slug: str, output_name: str, scene_label: str) -> None:
    specs = [
        (f"{scene_label}, scientific, normalna geometryczna", f"shading-normal-study-{scene_slug}-tessellation-error-*-rmscientific-nmgeometric-*-method.png"),
        (f"{scene_label}, scientific, normalna wygładzona", f"shading-normal-study-{scene_slug}-tessellation-error-*-rmscientific-nmsmooth-*-method.png"),
        (f"{scene_label}, PBR, normalna geometryczna", f"shading-normal-study-{scene_slug}-tessellation-error-*-rmrealistic-nmgeometric-*-method.png"),
        (f"{scene_label}, PBR, normalna wygładzona", f"shading-normal-study-{scene_slug}-tessellation-error-*-rmrealistic-nmsmooth-*-method.png"),
    ]
    paths: list[tuple[str, Path]] = []
    for title, pattern in specs:
        path = find_one(capture_dir, pattern)
        if path:
            paths.append((title, path))
    if len(paths) >= 2:
        image_panel(paths, out_dir, output_name, columns=2)


def shading_normal_gallery(capture_dir: Path, out_dir: Path) -> None:
    shading_normal_gallery_for_scene(capture_dir, out_dir, "glacial-valley", "shading_normal_gallery_glacial", "Glacial Valley")
    shading_normal_gallery_for_scene(capture_dir, out_dir, "cliff-band", "shading_normal_gallery_cliffband", "Cliff Band")
    shading_normal_gallery_for_scene(capture_dir, out_dir, "alpine-escarpment", "shading_normal_gallery_alpine", "Alpine Escarpment")


def pbr_roughness_gallery(capture_dir: Path, out_dir: Path) -> None:
    specs = [
        ("chropowatość 0.12", "context-sensitivity-glacial-valley-tessellation-context-aware-*-r12-*-realistic.png"),
        ("chropowatość 0.45", "context-sensitivity-glacial-valley-tessellation-context-aware-*-r45-*-realistic.png"),
        ("chropowatość 0.85", "context-sensitivity-glacial-valley-tessellation-context-aware-*-r85-*-realistic.png"),
    ]
    paths: list[tuple[str, Path]] = []
    for title, pattern in specs:
        path = find_one(capture_dir, pattern)
        if path:
            paths.append((title, path))
    if len(paths) == 3:
        image_panel(paths, out_dir, "pbr_roughness_gallery", columns=1)


def strong_baseline_gallery(capture_dir: Path, out_dir: Path) -> None:
    specs = [
        ("Odniesienie", "thesis-figure-glacial-valley-tessellation-error-*-reference.png"),
        ("RMS błędu rekonstrukcji", "thesis-figure-glacial-valley-tessellation-variance-baseline-*-method.png"),
        ("Maksymalny rzutowany błąd rekonstrukcji", "thesis-figure-glacial-valley-tessellation-error-*-method.png"),
        ("Ograniczenie błędu normalnej", "thesis-figure-glacial-valley-tessellation-normal-bound-*-method.png"),
        ("Ograniczenie radiometryczne", "thesis-figure-glacial-valley-tessellation-context-aware-*-method.png"),
    ]
    paths: list[tuple[str, Path]] = []
    for title, pattern in specs:
        path = find_one(capture_dir, pattern)
        if path:
            paths.append((title, path))
    image_panel(paths, out_dir, "glacial_strong_baselines", columns=1)

    ref = find_one(capture_dir, "thesis-figure-glacial-valley-tessellation-error-*-reference.png")
    method = find_one(capture_dir, "thesis-figure-glacial-valley-tessellation-error-*-method.png")
    error = find_one(capture_dir, "thesis-figure-glacial-valley-tessellation-error-*-rgb-error.png")
    if ref and method and error:
        raw_error_comparison(ref, method, error, out_dir, "glacial_projected_error_diff")

def silhouette_gallery(capture_dir: Path, out_dir: Path) -> None:
    specs = [
        ("Odniesienie", "silhouette-stress-sharp-ridge-tessellation-error-*-reference.png"),
        ("RMS błędu rekonstrukcji", "silhouette-stress-sharp-ridge-tessellation-variance-baseline-*-method.png"),
        ("Maksymalny rzutowany błąd rekonstrukcji", "silhouette-stress-sharp-ridge-tessellation-error-*-method.png"),
        ("Ograniczenie błędu normalnej", "silhouette-stress-sharp-ridge-tessellation-normal-bound-*-method.png"),
        ("Ograniczenie radiometryczne", "silhouette-stress-sharp-ridge-tessellation-context-aware-*-method.png"),
    ]
    paths: list[tuple[str, Path]] = []
    for title, pattern in specs:
        path = find_one(capture_dir, pattern)
        if path:
            paths.append((title, path))
    image_panel(paths, out_dir, "sharp_ridge_silhouette", columns=2)


def negative_control_gallery(capture_dir: Path, out_dir: Path) -> None:
    specs: list[tuple[str, str]] = [
        ("Smooth Hills: odniesienie", "negative-control-smooth-hills-tessellation-error-*-reference.png"),
        ("Smooth Hills: rzutowany błąd rekonstrukcji", "negative-control-smooth-hills-tessellation-error-*-method.png"),
        ("Badlands: odniesienie", "negative-control-badlands-tessellation-error-*-reference.png"),
        ("Badlands: rzutowany błąd rekonstrukcji", "negative-control-badlands-tessellation-error-*-method.png"),
    ]
    paths: list[tuple[str, Path]] = []
    for title, pattern in specs:
        path = find_one(capture_dir, pattern)
        if path:
            paths.append((title, path))
    image_panel(paths, out_dir, "negative_controls", columns=2)


def plot_matched_by_scene(rows: list[dict[str, str]], out_dir: Path) -> None:
    selected = [r for r in rows if r.get("baseline") == "tessellation-variance-baseline" and r.get("candidate") == "tessellation-error"]
    scenes = sorted({r["scene"] for r in selected})
    if not scenes:
        return
    gpu = []
    tri = []
    for scene in scenes:
        sub = [r for r in selected if r["scene"] == scene]
        gpu.append(100.0 * float(np.median([f(r, "gpu_change_fraction") for r in sub])))
        tri.append(100.0 * float(np.median([f(r, "triangle_change_fraction") for r in sub])))
    y = np.arange(len(scenes))
    fig, ax = plt.subplots(figsize=(7.2, 3.8))
    ax.scatter(gpu, y, marker="o", label="Czas GPU")
    ax.scatter(tri, y, marker="s", label="Liczba trójkątów")
    for idx, scene in enumerate(scenes):
        ax.plot([gpu[idx], tri[idx]], [idx, idx], linewidth=0.8, alpha=0.6)
    ax.axvline(0.0, linewidth=1.0)
    ax.set_yticks(y, [s.replace("-", " ").title() for s in scenes])
    ax.set_xlabel("Mediana zmiany względem baseline'u RMS błędu rekonstrukcji [%]")
    ax.legend(frameon=False)
    ax.grid(axis="x", alpha=0.25)
    fig.tight_layout()
    save_figure(fig, out_dir, "matched_variance_by_scene")


def plot_pareto(cases: list[dict[str, str]], out_dir: Path) -> None:
    rows = [r for r in cases if r.get("study") == "strong-baseline" and r.get("scene") == "glacial-valley"]
    if not rows:
        return
    methods = [
        "tessellation-distance",
        "tessellation-variance-baseline",
        "tessellation-error",
        "tessellation-normal-bound",
        "tessellation-context-aware",
    ]
    markers = ["x", "s", "o", "^", "D"]
    labels = {
        "tessellation-distance": "odległość",
        "tessellation-variance-baseline": "RMS błędu rekonstrukcji",
        "tessellation-error": "maks. rzutowany błąd rekonstrukcji",
        "tessellation-normal-bound": "+ próg normalnej",
        "tessellation-context-aware": "+ ograniczenie radiometryczne",
    }
    fig, ax = plt.subplots(figsize=(6.8, 4.3))
    for method, marker in zip(methods, markers):
        sub = [r for r in rows if r.get("method") == method]
        x = [f(r, "gpu_median_ms") for r in sub]
        y = [f(r, "image_foreground_rmse") for r in sub]
        if x:
            ax.scatter(x, y, marker=marker, label=labels[method], alpha=0.85)
    ax.set_xlabel("Mediana czasu GPU [ms]")
    ax.set_ylabel("RMSE w obszarze terenu")
    ax.grid(alpha=0.25)
    ax.legend(frameon=False, fontsize=8)
    fig.tight_layout()
    save_figure(fig, out_dir, "glacial_pareto")


def plot_calibration(rows: list[dict[str, str]], out_dir: Path) -> None:
    selected = [r for r in rows if r.get("scene") == "mixed-frequency" and r.get("method") == "tessellation-error" and abs(f(r, "distance") - 13.0) < 1e-5]
    selected.sort(key=lambda r: f(r, "error_budget_px"))
    if not selected:
        return
    budget = [f(r, "error_budget_px") for r in selected]
    tri = [f(r, "triangles") for r in selected]
    rmse = [f(r, "image_foreground_rmse") for r in selected]
    fig, ax = plt.subplots(figsize=(6.8, 4.0))
    line1 = ax.plot(budget, tri, marker="o", label="Liczba trójkątów")
    ax.set_xscale("log", base=2)
    ax.set_xlabel("Dopuszczalny błąd projekcyjny [px]")
    ax.set_ylabel("Liczba trójkątów")
    ax2 = ax.twinx()
    line2 = ax2.plot(budget, rmse, marker="s", linestyle="--", label="RMSE w obszarze terenu")
    ax2.set_ylabel("RMSE w obszarze terenu")
    lines = line1 + line2
    ax.legend(lines, [line.get_label() for line in lines], frameon=False, fontsize=8)
    ax.grid(alpha=0.25)
    fig.tight_layout()
    save_figure(fig, out_dir, "calibration_mixed_frequency")


def plot_resolution(rows: list[dict[str, str]], out_dir: Path) -> None:
    if not rows:
        return
    fig, ax = plt.subplots(figsize=(6.8, 4.0))
    for method, marker in [("tessellation-error", "o"), ("tessellation-context-aware", "s")]:
        sub = sorted([r for r in rows if r.get("method") == method], key=lambda r: int(float(r["height"])))
        if not sub:
            continue
        heights = [int(float(r["height"])) for r in sub]
        triangles = [f(r, "triangles_median") for r in sub]
        label = "maks. rzutowany błąd rekonstrukcji" if method == "tessellation-error" else "ograniczenie radiometryczne"
        ax.plot(heights, triangles, marker=marker, label=label)
    ax.set_xlabel("Wysokość obrazu [px]")
    ax.set_ylabel("Liczba trójkątów")
    ax.grid(alpha=0.25)
    ax.legend(frameon=False)
    fig.tight_layout()
    save_figure(fig, out_dir, "resolution_scaling")


def plot_context_gain(rows: list[dict[str, str]], out_dir: Path) -> None:
    if not rows:
        return
    x = [100.0 * f(r, "gpu_change_fraction") for r in rows]
    y = [100.0 * f(r, "rmse_change_fraction") for r in rows]
    fig, ax = plt.subplots(figsize=(6.6, 4.2))
    ax.scatter(x, y, alpha=0.75)
    ax.axvline(0.0, linewidth=1.0)
    ax.axhline(0.0, linewidth=1.0)
    ax.set_xlabel("Zmiana czasu GPU względem wariantu z progiem normalnej [%]")
    ax.set_ylabel("Zmiana RMSE w obszarze terenu [%]")
    ax.text(0.02, 0.04, "mniejszy błąd poniżej osi", transform=ax.transAxes, fontsize=8)
    ax.grid(alpha=0.25)
    fig.tight_layout()
    save_figure(fig, out_dir, "context_gain_scatter")


def plot_boundary_scatter(rows: list[dict[str, str]], out_dir: Path) -> None:
    selected = [r for r in rows if r.get("baseline") == "tessellation-distance" and r.get("candidate") == "tessellation-error"]
    if not selected:
        return
    x = np.array([f(r, "baseline_boundary_p95_px") for r in selected])
    y = np.array([f(r, "candidate_boundary_p95_px") for r in selected])
    finite = np.isfinite(x) & np.isfinite(y)
    x = x[finite]
    y = y[finite]
    if x.size == 0:
        return
    limit = max(float(np.max(x)), float(np.max(y)), 1.0)
    fig, ax = plt.subplots(figsize=(5.4, 5.0))
    ax.scatter(x, y, alpha=0.7)
    ax.plot([0, limit], [0, limit], linestyle="--", linewidth=1.0)
    ax.set_xlim(0, limit * 1.05)
    ax.set_ylim(0, limit * 1.05)
    ax.set_xlabel("Distance LOD boundary p95 [px]")
    ax.set_ylabel("Projected-error boundary p95 [px]")
    ax.grid(alpha=0.25)
    fig.tight_layout()
    save_figure(fig, out_dir, "boundary_distance_vs_projected")


def plot_constraint_sweeps(rows: list[dict[str, str]], out_dir: Path) -> None:
    normal = [r for r in rows if r.get("method") == "tessellation-normal-bound"]
    normal.sort(key=lambda r: f(r, "normal_budget_degrees"))
    if normal:
        x = [f(r, "normal_budget_degrees") for r in normal]
        tri = [f(r, "triangles_median") for r in normal]
        rmse = [f(r, "image_foreground_rmse") for r in normal]
        fig, ax = plt.subplots(figsize=(6.8, 4.0))
        l1 = ax.plot(x, tri, marker="o", label="Triangles")
        ax.set_xscale("log", base=2)
        ax.set_xlabel("Normal budget [degrees]")
        ax.set_ylabel("Liczba trójkątów")
        ax2 = ax.twinx()
        l2 = ax2.plot(x, rmse, marker="s", linestyle="--", label="Foreground RMSE")
        ax2.set_ylabel("RMSE w obszarze terenu")
        lines = l1 + l2
        ax.legend(lines, [line.get_label() for line in lines], frameon=False, fontsize=8)
        ax.grid(alpha=0.25)
        fig.tight_layout()
        save_figure(fig, out_dir, "normal_budget_sweep")

    context = [r for r in rows if r.get("method") == "tessellation-context-aware" and abs(f(r, "scientific_roughness") - 0.45) < 1e-4]
    context.sort(key=lambda r: f(r, "radiance_budget"))
    if context:
        x = [f(r, "radiance_budget") for r in context]
        tri = [f(r, "triangles_median") for r in context]
        rmse = [f(r, "image_foreground_rmse") for r in context]
        fig, ax = plt.subplots(figsize=(6.8, 4.0))
        l1 = ax.plot(x, tri, marker="o", label="Triangles")
        ax.set_xscale("log")
        ax.set_xlabel("Radiance budget")
        ax.set_ylabel("Liczba trójkątów")
        ax2 = ax.twinx()
        l2 = ax2.plot(x, rmse, marker="s", linestyle="--", label="Foreground RMSE")
        ax2.set_ylabel("RMSE w obszarze terenu")
        lines = l1 + l2
        ax.legend(lines, [line.get_label() for line in lines], frameon=False, fontsize=8)
        ax.grid(alpha=0.25)
        fig.tight_layout()
        save_figure(fig, out_dir, "radiance_budget_sweep")


def controller_flow(out_dir: Path) -> None:
    # The diagram separates the run-time decision path from experimental validation.
    # Measured reference error is not fed back into the controller in the same frame.
    fig, ax = plt.subplots(figsize=(6.2, 7.6))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 14)
    ax.axis("off")

    steps = [
        (11.6, "Dane powierzchni", "pole wysokości i lokalne próbki rekonstrukcji"),
        (9.35, "Stan widoku", "kamera, FOV, rozdzielczość, oświetlenie"),
        (7.10, "Estymacja błędu", "projekcja ekranowa i kryteria dopuszczalności"),
        (4.85, "Wybór wariantu", "minimalny koszt spośród wariantów dopuszczalnych"),
        (2.60, "Renderowanie i pomiar", "obraz HDR, głębokość, kontur, czas GPU"),
    ]
    x, width, height = 1.35, 7.30, 1.40
    for y, title, subtitle in steps:
        patch = FancyBboxPatch(
            (x, y), width, height,
            boxstyle="round,pad=0.08,rounding_size=0.08",
            facecolor=(0.985, 0.985, 0.985), edgecolor=(0.18, 0.18, 0.18), linewidth=1.1,
        )
        ax.add_patch(patch)
        ax.text(x + width / 2, y + 0.86, title, ha="center", va="center", fontsize=10, fontweight="semibold")
        ax.plot([x + 0.75, x + width - 0.75], [y + 0.63, y + 0.63], linewidth=0.55, color=(0.58, 0.58, 0.58))
        ax.text(x + width / 2, y + 0.34, subtitle, ha="center", va="center", fontsize=8.0)

    for upper, lower in zip(steps[:-1], steps[1:]):
        ax.add_patch(FancyArrowPatch(
            (5.0, upper[0] - 0.08), (5.0, lower[0] + height + 0.08),
            arrowstyle="-|>", mutation_scale=12, linewidth=1.0,
        ))

    # External validation path: it documents how the estimator is assessed, not an
    # on-line feedback loop in the rendering algorithm.
    ax.add_patch(FancyBboxPatch(
        (1.75, 0.35), 6.5, 1.15,
        boxstyle="round,pad=0.06,rounding_size=0.06",
        facecolor=(0.96, 0.96, 0.96), edgecolor=(0.35, 0.35, 0.35), linewidth=0.9, linestyle="--",
    ))
    ax.text(5.0, 1.04, "Walidacja eksperymentalna", ha="center", va="center", fontsize=9, fontweight="semibold")
    ax.text(5.0, 0.70, "porównanie z obrazem referencyjnym i analiza błędu estymatora",
            ha="center", va="center", fontsize=7.8)
    ax.add_patch(FancyArrowPatch(
        (5.0, 2.55), (5.0, 1.55), arrowstyle="-|>", mutation_scale=11, linewidth=0.9, linestyle="--"
    ))
    fig.tight_layout()
    save_figure(fig, out_dir, "controller_flow")

def representation_capability(out_dir: Path) -> None:
    fig, ax = plt.subplots(figsize=(7.2, 3.4))
    ax.axis("off")
    columns = ["State", "Primary shape", "Silhouette", "Parallax", "Fine shading", "Typical cost"]
    data = [
        ["Explicit geometry", "yes", "yes", "yes", "yes", "highest"],
        ["Height / displacement", "yes", "conditional", "yes", "yes", "medium"],
        ["Normal / material", "no", "no", "no", "yes", "low"],
        ["Aggregate statistics", "no", "no", "no", "filtered only", "lowest"],
    ]
    table = ax.table(cellText=data, colLabels=columns, loc="center", cellLoc="center")
    table.auto_set_font_size(False)
    table.set_fontsize(8)
    table.scale(1.0, 1.45)
    ax.set_title("Representation capability and admission constraints", fontsize=10, pad=10)
    fig.tight_layout()
    save_figure(fig, out_dir, "representation_capability")


def pbr_scene_gallery(capture_dir: Path, out_dir: Path) -> None:
    specs = [
        ("Smooth Hills", "pbr-figure-smooth-hills-tessellation-context-aware-*-method.png"),
        ("Sharp Ridge", "pbr-figure-sharp-ridge-tessellation-context-aware-*-method.png"),
        ("Mixed Frequency", "pbr-figure-mixed-frequency-tessellation-context-aware-*-method.png"),
        ("Cliff Band", "pbr-figure-cliff-band-tessellation-context-aware-*-method.png"),
        ("Eroded Mountain", "pbr-figure-eroded-mountain-tessellation-context-aware-*-method.png"),
        ("Plateau Canyon", "pbr-figure-plateau-canyon-tessellation-context-aware-*-method.png"),
        ("Alpine Escarpment", "pbr-figure-alpine-escarpment-tessellation-context-aware-*-method.png"),
        ("Glacial Valley", "pbr-figure-glacial-valley-tessellation-context-aware-*-method.png"),
        ("River Basin", "pbr-figure-river-basin-tessellation-context-aware-*-method.png"),
        ("Badlands", "pbr-figure-badlands-tessellation-context-aware-*-method.png"),
        ("Fractured Highlands", "pbr-figure-fractured-highlands-tessellation-context-aware-*-method.png"),
    ]
    paths: list[tuple[str, Path]] = []
    for title, pattern in specs:
        path = find_one(capture_dir, pattern)
        if path:
            paths.append((title, path))
    if len(paths) >= 2:
        image_panel(paths, out_dir, "scene_gallery_full_pbr", columns=1)


def pbr_candidate_views(capture_dir: Path, out_dir: Path) -> None:
    cliff = []
    for pattern, label in [
        ("pbr-figure-cliff-band-tessellation-context-aware-*-yaw110-*-method.png", "Cliff Band 110°"),
        ("pbr-figure-cliff-band-tessellation-context-aware-*-yaw125-*-method.png", "Cliff Band 125°"),
        ("pbr-figure-cliff-band-tessellation-context-aware-*-yaw140-*-method.png", "Cliff Band 140°"),
    ]:
        path = find_one(capture_dir, pattern)
        if path:
            cliff.append((label, path))
    if cliff:
        image_panel(cliff, out_dir, "cliff_band_pbr_candidates", columns=3)

    alpine = []
    for pattern, label in [
        ("pbr-figure-alpine-escarpment-tessellation-context-aware-*-yaw118-*-method.png", "Alpine 118°"),
        ("pbr-figure-alpine-escarpment-tessellation-context-aware-*-yaw133-*-method.png", "Alpine 133°"),
        ("pbr-figure-alpine-escarpment-tessellation-context-aware-*-yaw148-*-method.png", "Alpine 148°"),
        ("pbr-figure-alpine-escarpment-tessellation-context-aware-*-yaw163-*-method.png", "Alpine 163°"),
    ]:
        path = find_one(capture_dir, pattern)
        if path:
            alpine.append((label, path))
    if alpine:
        image_panel(alpine, out_dir, "alpine_escarpment_pbr_candidates", columns=2)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate thesis-ready plots and qualitative panels.")
    parser.add_argument("results", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    capture_dir = args.results / "captures"

    cases = read_csv(args.results / "cases.csv")
    matched = read_csv(args.results / "matched_quality.csv")
    calibration = read_csv(args.results / "calibration.csv")
    context = read_csv(args.results / "context_gain.csv")
    resolution = read_csv(args.results / "resolution_scaling.csv")
    constraints = read_csv(args.results / "constraint_sweeps.csv")

    scene_gallery(capture_dir, args.output)
    pbr_scene_gallery(capture_dir, args.output)
    pbr_candidate_views(capture_dir, args.output)
    single_scene_overviews(capture_dir, args.output)
    alpine_camera_candidates(capture_dir, args.output)
    cliffband_camera_candidates(capture_dir, args.output)
    diagnostic_gallery(capture_dir, args.output)
    lod_heatmap_figure(capture_dir, args.output)
    pbr_roughness_gallery(capture_dir, args.output)
    shading_normal_gallery(capture_dir, args.output)
    strong_baseline_gallery(capture_dir, args.output)
    silhouette_gallery(capture_dir, args.output)
    negative_control_gallery(capture_dir, args.output)
    plot_matched_by_scene(matched, args.output)
    plot_pareto(cases, args.output)
    plot_calibration(calibration, args.output)
    plot_resolution(resolution, args.output)
    plot_context_gain(context, args.output)
    plot_boundary_scatter(matched, args.output)
    plot_constraint_sweeps(constraints, args.output)
    controller_flow(args.output)
    representation_capability(args.output)

    generated = sorted(path.name for path in args.output.iterdir() if path.is_file())
    (args.output / "FIGURES_GENERATED.txt").write_text("\n".join(generated) + "\n", encoding="utf-8")
    print(f"generated {len(generated)} files in {args.output}")


if __name__ == "__main__":
    main()
