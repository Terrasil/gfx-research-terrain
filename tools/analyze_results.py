from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Iterable


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def percentile(values: list[float], p: float) -> float:
    if not values:
        return float("nan")
    xs = sorted(values)
    pos = p * (len(xs) - 1)
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return xs[lo]
    t = pos - lo
    return xs[lo] * (1.0 - t) + xs[hi] * t


def fmean(values: Iterable[float]) -> float:
    xs = list(values)
    return statistics.fmean(xs) if xs else float("nan")


def median(values: Iterable[float]) -> float:
    xs = list(values)
    return statistics.median(xs) if xs else float("nan")


def write_csv(path: Path, rows: list[dict[str, object]], fieldnames: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def summarize_raw(raw: list[dict[str, str]]) -> list[dict[str, object]]:
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in raw:
        grouped[row["case"]].append(row)

    out: list[dict[str, object]] = []
    for case, rows in sorted(grouped.items(), key=lambda item: int(item[0])):
        gpu = [float(r["gpu_ms"]) for r in rows]
        triangles = [float(r["triangles"]) for r in rows]
        transitions = [float(r["lod_transitions"]) for r in rows]
        max_geom = [float(r["max_geometric_error_px"]) for r in rows]
        max_control = [float(r["max_controller_error_px"]) for r in rows]
        first = rows[0]
        row: dict[str, object] = {
            "case": case,
            "study": first["study"],
            "mode": first["mode"],
            "scene": first["scene"],
            "method": first["method"],
            "base_distance": first["base_distance"],
            "motion_amplitude": first["motion_amplitude"],
            "error_budget_px": first["error_budget_px"],
            "distance_lod_bias": first["distance_lod_bias"],
            "hysteresis": first["hysteresis"],
            "width": first["width"],
            "height": first["height"],
            "samples": len(rows),
            "gpu_mean_ms": fmean(gpu),
            "gpu_median_ms": median(gpu),
            "gpu_stddev_ms": statistics.stdev(gpu) if len(gpu) > 1 else 0.0,
            "gpu_p95_ms": percentile(gpu, 0.95),
            "triangles_median": median(triangles),
            "transitions_total": int(sum(transitions)),
            "transitions_mean": fmean(transitions),
            "max_geometric_error_px_max": max(max_geom) if max_geom else float("nan"),
            "max_controller_error_px_max": max(max_control) if max_control else float("nan"),
        }
        out.append(row)
    return out


def pareto_flags(rows: list[dict[str, object]], cost_key: str, error_key: str) -> dict[str, bool]:
    flags: dict[str, bool] = {}
    for row in rows:
        case = str(row["case"])
        cost = float(row[cost_key])
        error = float(row[error_key])
        dominated = False
        for other in rows:
            if other is row:
                continue
            other_cost = float(other[cost_key])
            other_error = float(other[error_key])
            if other_cost <= cost and other_error <= error and (other_cost < cost or other_error < error):
                dominated = True
                break
        flags[case] = not dominated
    return flags


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze gfx-research-terrain publication output")
    parser.add_argument("results", type=Path)
    args = parser.parse_args()

    raw_path = args.results / "raw.csv"
    quality_path = args.results / "quality.csv"
    if not raw_path.exists() or not quality_path.exists():
        raise SystemExit("raw.csv and quality.csv are required")

    raw = read_rows(raw_path)
    quality = read_rows(quality_path)
    if not raw:
        raise SystemExit("raw.csv contains no samples")

    summary = summarize_raw(raw)
    summary_fields = list(summary[0].keys())
    write_csv(args.results / "summary.csv", summary, summary_fields)

    quality_by_case = {row["case"]: row for row in quality}
    cases: list[dict[str, object]] = []
    for row in summary:
        merged = dict(row)
        q = quality_by_case.get(str(row["case"]))
        if q:
            for key, value in q.items():
                if key not in merged:
                    merged[key] = value
        cases.append(merged)

    quality_fields = [
        "camera_distance",
        "image_mae",
        "image_rmse",
        "image_relative_rmse",
        "image_foreground_rmse",
        "image_psnr",
        "image_max_abs",
        "depth_mae",
        "depth_rmse",
        "depth_max_abs",
        "rgba_exact",
        "depth_exact",
    ]
    case_fields = summary_fields + [field for field in quality_fields if field not in summary_fields]
    write_csv(args.results / "cases.csv", cases, case_fields)

    primary = [
        row for row in cases
        if row.get("study") == "primary" and row.get("image_foreground_rmse", "") != ""
    ]
    pareto_rows: list[dict[str, object]] = []
    grouped_primary: dict[tuple[str, str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in primary:
        grouped_primary[(
            str(row["scene"]),
            str(row["base_distance"]),
            str(row["width"]),
            str(row["height"]),
        )].append(row)

    for group_rows in grouped_primary.values():
        triangle_flags = pareto_flags(group_rows, "triangles_median", "image_foreground_rmse")
        gpu_flags = pareto_flags(group_rows, "gpu_median_ms", "image_foreground_rmse")
        for row in group_rows:
            pareto_rows.append({
                "case": row["case"],
                "scene": row["scene"],
                "method": row["method"],
                "base_distance": row["base_distance"],
                "error_budget_px": row["error_budget_px"],
                "distance_lod_bias": row["distance_lod_bias"],
                "gpu_median_ms": row["gpu_median_ms"],
                "triangles_median": row["triangles_median"],
                "image_foreground_rmse": row["image_foreground_rmse"],
                "pareto_triangles": int(triangle_flags[str(row["case"])]),
                "pareto_gpu": int(gpu_flags[str(row["case"])]),
            })
    pareto_rows.sort(key=lambda row: int(str(row["case"])))
    pareto_fields = [
        "case", "scene", "method", "base_distance", "error_budget_px", "distance_lod_bias",
        "gpu_median_ms", "triangles_median", "image_foreground_rmse", "pareto_triangles", "pareto_gpu",
    ]
    write_csv(args.results / "pareto.csv", pareto_rows, pareto_fields)

    temporal_rows: list[dict[str, object]] = []
    for row in cases:
        if row.get("study") != "temporal-stability":
            continue
        temporal_rows.append({
            "case": row["case"],
            "scene": row["scene"],
            "method": row["method"],
            "hysteresis": row["hysteresis"],
            "motion_amplitude": row["motion_amplitude"],
            "transitions_total": row["transitions_total"],
            "transitions_mean": row["transitions_mean"],
            "gpu_median_ms": row["gpu_median_ms"],
            "image_foreground_rmse": row.get("image_foreground_rmse", ""),
        })
    temporal_fields = [
        "case", "scene", "method", "hysteresis", "motion_amplitude", "transitions_total",
        "transitions_mean", "gpu_median_ms", "image_foreground_rmse",
    ]
    write_csv(args.results / "temporal.csv", temporal_rows, temporal_fields)

    null_rows: list[dict[str, object]] = []
    null_failed = False
    for row in quality:
        if row.get("study") != "null-equivalence":
            continue
        passed = row.get("rgba_exact") == "1" and row.get("depth_exact") == "1"
        null_failed |= not passed
        null_rows.append({
            "case": row["case"],
            "scene": row["scene"],
            "rgba_exact": row["rgba_exact"],
            "depth_exact": row["depth_exact"],
            "pass": int(passed),
        })
    write_csv(args.results / "null.csv", null_rows, ["case", "scene", "rgba_exact", "depth_exact", "pass"])

    print(f"wrote {args.results / 'summary.csv'}")
    print(f"wrote {args.results / 'cases.csv'}")
    print(f"wrote {args.results / 'pareto.csv'}")
    print(f"wrote {args.results / 'temporal.csv'}")
    print(f"wrote {args.results / 'null.csv'}")
    if null_failed:
        raise SystemExit("NULL TEST FAILED: do not use this run for publication claims")


if __name__ == "__main__":
    main()
