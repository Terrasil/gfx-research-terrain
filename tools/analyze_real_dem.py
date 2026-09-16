from __future__ import annotations

import csv
import math
import statistics
import sys
from pathlib import Path


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        raise FileNotFoundError(path)
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def f(row: dict[str, str], key: str, default: float = math.nan) -> float:
    try:
        return float(row[key])
    except (KeyError, TypeError, ValueError):
        return default


def i(row: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(float(row[key]))
    except (KeyError, TypeError, ValueError):
        return default


def median(values: list[float]) -> float:
    vals = [v for v in values if math.isfinite(v)]
    return statistics.median(vals) if vals else math.nan


def pct_change(candidate: float, baseline: float) -> float:
    if not math.isfinite(candidate) or not math.isfinite(baseline) or baseline == 0.0:
        return math.nan
    return (candidate / baseline - 1.0) * 100.0


def build_case_summary(raw_rows: list[dict[str, str]], quality_rows: list[dict[str, str]]) -> list[dict[str, object]]:
    raw_by_case: dict[int, list[dict[str, str]]] = {}
    for row in raw_rows:
        raw_by_case.setdefault(i(row, "case", -1), []).append(row)
    quality_by_case = {i(row, "case", -1): row for row in quality_rows}

    summaries: list[dict[str, object]] = []
    for case_id in sorted(raw_by_case):
        rows = raw_by_case[case_id]
        first = rows[0]
        q = quality_by_case.get(case_id, {})
        summaries.append({
            "case": case_id,
            "study": first.get("study", ""),
            "mode": first.get("mode", ""),
            "scene": first.get("scene", ""),
            "method": first.get("method", ""),
            "render_mode": first.get("render_mode", ""),
            "shading_normal_mode": first.get("shading_normal_mode", ""),
            "camera_distance": f(first, "base_distance"),
            "error_budget_px": f(first, "error_budget_px"),
            "distance_lod_bias": i(first, "distance_lod_bias"),
            "gpu_ms_median": median([f(r, "gpu_ms") for r in rows]),
            "triangles_median": median([f(r, "triangles") for r in rows]),
            "foreground_rmse": f(q, "image_foreground_rmse"),
            "depth_rmse": f(q, "depth_rmse"),
            "boundary_p95_px": f(q, "boundary_p95_distance_px"),
            "coverage_mismatch_fraction": f(q, "coverage_mismatch_fraction"),
            "rgba_exact": i(q, "rgba_exact", -1),
            "depth_exact": i(q, "depth_exact", -1),
        })
    return summaries


def write_summary(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def matched_pairs(rows: list[dict[str, object]], candidate_method: str, baseline_method: str, tolerance: float = 0.15) -> list[dict[str, float]]:
    scientific = [
        r for r in rows
        if r["mode"] == "static"
        and r["render_mode"] == "scientific"
        and math.isfinite(float(r["foreground_rmse"]))
    ]
    candidates = [r for r in scientific if r["method"] == candidate_method]
    baselines = [r for r in scientific if r["method"] == baseline_method]
    pairs: list[dict[str, float]] = []
    for cand in candidates:
        same_distance = [b for b in baselines if abs(float(b["camera_distance"]) - float(cand["camera_distance"])) < 1e-6]
        if not same_distance:
            continue
        rmse_c = float(cand["foreground_rmse"])
        best = min(same_distance, key=lambda b: abs(float(b["foreground_rmse"]) - rmse_c))
        rmse_b = float(best["foreground_rmse"])
        gap = abs(rmse_c - rmse_b) / max(abs(rmse_b), 1e-12)
        if gap > tolerance:
            continue
        pairs.append({
            "distance": float(cand["camera_distance"]),
            "candidate_rmse": rmse_c,
            "baseline_rmse": rmse_b,
            "quality_gap_pct": gap * 100.0,
            "gpu_change_pct": pct_change(float(cand["gpu_ms_median"]), float(best["gpu_ms_median"])),
            "triangles_change_pct": pct_change(float(cand["triangles_median"]), float(best["triangles_median"])),
            "boundary_change_px": float(cand["boundary_p95_px"]) - float(best["boundary_p95_px"]),
        })
    return pairs


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: analyze_real_dem.py <results-dir>", file=sys.stderr)
        return 2

    root = Path(sys.argv[1])
    raw = read_csv(root / "raw.csv")
    quality = read_csv(root / "quality.csv")
    rows = build_case_summary(raw, quality)
    if not rows:
        raise RuntimeError("No real-DEM benchmark cases found.")

    if any(r["scene"] != "mount-st-helens-dem" for r in rows):
        raise RuntimeError("The real-DEM suite contains an unexpected scene.")

    write_summary(root / "real_dem_case_summary.csv", rows)

    projected_vs_rms = matched_pairs(rows, "tessellation-error", "tessellation-variance-baseline", 0.15)
    projected_vs_distance = matched_pairs(rows, "tessellation-error", "tessellation-distance", 0.15)

    if projected_vs_rms:
        write_summary(root / "real_dem_matched_projected_vs_rms.csv", projected_vs_rms)
    if projected_vs_distance:
        write_summary(root / "real_dem_matched_projected_vs_distance.csv", projected_vs_distance)

    null_rows = [r for r in rows if r["mode"] == "null"]
    null_ok = bool(null_rows) and all(r["rgba_exact"] == 1 and r["depth_exact"] == 1 for r in null_rows)

    lines = [
        "# Real DEM validation summary",
        "",
        "Dataset: Mount St. Helens crop prepared from the USGS 3DEP 1/3 arc-second seamless DEM tile USGS_13_n47w123_20250813.tif.",
        "The benchmark uses a seamless 1921 x 1921 regular heightfield in NAD83 / UTM zone 10N at 10 m sample spacing, spanning 19.2 km around the summit. The preparation step rejects missing output samples and does not fill survey-footprint holes.",
        "This suite is supplementary external validation and does not replace or enlarge the frozen 688-case core matrix.",
        "",
        f"Cases: {len(rows)}",
        f"Null-equivalence gate: {'PASS' if null_ok else 'FAIL'}",
        "",
    ]

    def append_pair_summary(title: str, pairs: list[dict[str, float]]) -> None:
        lines.append(f"## {title}")
        if not pairs:
            lines.append("No pairs met the 15% foreground-RMSE matching tolerance.")
            lines.append("")
            return
        gpu = [p["gpu_change_pct"] for p in pairs]
        tris = [p["triangles_change_pct"] for p in pairs]
        gaps = [p["quality_gap_pct"] for p in pairs]
        boundary = [p["boundary_change_px"] for p in pairs]
        lines.extend([
            f"Matched pairs: {len(pairs)}",
            f"Median quality gap: {median(gaps):.3f}%",
            f"Median GPU change: {median(gpu):+.3f}%",
            f"Median triangle change: {median(tris):+.3f}%",
            f"Median boundary-p95 change: {median(boundary):+.3f} px",
            "",
        ])

    append_pair_summary("Projected maximum residual vs RMS residual baseline", projected_vs_rms)
    append_pair_summary("Projected maximum residual vs distance LOD", projected_vs_distance)

    pbr = [r for r in rows if r["render_mode"] == "realistic"]
    lines.extend([
        "## PBR captures",
        f"PBR capture cases: {len(pbr)}",
        "These images are qualitative supplements; the primary external-validation comparison uses the controlled scientific view.",
        "",
    ])

    (root / "real_dem_report.md").write_text("\n".join(lines), encoding="utf-8")
    print(root / "real_dem_report.md")
    return 0 if null_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
