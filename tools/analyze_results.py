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


def as_float(row: dict[str, object], key: str, default: float = float("nan")) -> float:
    value = row.get(key, "")
    if value in (None, ""):
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


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
        adaptation_cpu = [float(r.get("adaptation_cpu_ms", 0)) for r in rows]
        first = rows[0]
        row: dict[str, object] = {
            "case": case,
            "study": first["study"],
            "mode": first["mode"],
            "scene": first["scene"],
            "method": first["method"],
            "base_distance": first["base_distance"],
            "camera_yaw": first.get("camera_yaw", ""),
            "camera_pitch": first.get("camera_pitch", ""),
            "camera_fov_degrees": first.get("camera_fov_degrees", ""),
            "camera_target_x": first.get("camera_target_x", ""),
            "camera_target_z": first.get("camera_target_z", ""),
            "camera_target_height_offset": first.get("camera_target_height_offset", ""),
            "motion_amplitude": first["motion_amplitude"],
            "error_budget_px": first["error_budget_px"],
            "normal_budget_degrees": first.get("normal_budget_degrees", ""),
            "radiance_budget": first.get("radiance_budget", ""),
            "scientific_roughness": first.get("scientific_roughness", ""),
            "light_azimuth_degrees": first.get("light_azimuth_degrees", ""),
            "light_elevation_degrees": first.get("light_elevation_degrees", ""),
            "distance_lod_bias": first["distance_lod_bias"],
            "hysteresis": first["hysteresis"],
            "morph_band": first.get("morph_band", ""),
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
            "adaptive_leaf_count_median": median(float(r.get("adaptive_leaf_count", 0)) for r in rows),
            "morphed_vertices_median": median(float(r.get("morphed_vertices", 0)) for r in rows),
            "saturated_regions_max": max(float(r.get("saturated_regions", 0)) for r in rows),
            "mean_refinement_level_mean": fmean(float(r.get("mean_refinement_level", 0)) for r in rows),
            "adaptation_cpu_mean_ms": fmean(adaptation_cpu),
            "adaptation_cpu_median_ms": median(adaptation_cpu),
            "adaptation_cpu_p95_ms": percentile(adaptation_cpu, 0.95),
            "serial_cost_median_ms": median(gpu) + median(adaptation_cpu),
            "topology_open_internal_edges_max": max(
                float(r.get("topology_open_internal_edges", 0)) for r in rows
            ),
            "topology_nonmanifold_edges_max": max(
                float(r.get("topology_nonmanifold_edges", 0)) for r in rows
            ),
        }
        out.append(row)
    return out


def pareto_flags(rows: list[dict[str, object]], cost_key: str, error_key: str) -> dict[str, bool]:
    flags: dict[str, bool] = {}
    finite_rows = [
        row for row in rows
        if math.isfinite(as_float(row, cost_key)) and math.isfinite(as_float(row, error_key))
    ]
    for row in rows:
        case = str(row["case"])
        cost = as_float(row, cost_key)
        error = as_float(row, error_key)
        if not math.isfinite(cost) or not math.isfinite(error):
            flags[case] = False
            continue
        dominated = False
        for other in finite_rows:
            if other is row:
                continue
            other_cost = as_float(other, cost_key)
            other_error = as_float(other, error_key)
            if other_cost <= cost and other_error <= error and (other_cost < cost or other_error < error):
                dominated = True
                break
        flags[case] = not dominated
    return flags


def build_matched_quality(cases: list[dict[str, object]]) -> list[dict[str, object]]:
    baselines = {"tessellation-distance", "tessellation-variance-baseline"}
    candidates = {
        "tessellation-error",
        "tessellation-normal-bound",
        "tessellation-context-aware",
    }
    groups: dict[tuple[str, str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in cases:
        if row.get("study") != "strong-baseline":
            continue
        groups[(
            str(row["scene"]), str(row["base_distance"]), str(row["width"]), str(row["height"])
        )].append(row)

    out: list[dict[str, object]] = []
    for key, rows in groups.items():
        for candidate in candidates:
            candidate_rows = [r for r in rows if r.get("method") == candidate]
            for c in candidate_rows:
                c_error = as_float(c, "image_foreground_rmse")
                if not math.isfinite(c_error):
                    continue
                for baseline in baselines:
                    baseline_rows = [r for r in rows if r.get("method") == baseline]
                    if not baseline_rows:
                        continue
                    b = min(
                        baseline_rows,
                        key=lambda r: abs(math.log(max(as_float(r, "image_foreground_rmse"), 1e-12))
                                          - math.log(max(c_error, 1e-12))))
                    b_error = as_float(b, "image_foreground_rmse")
                    relative_gap = abs(b_error - c_error) / max(c_error, b_error, 1e-12)
                    if relative_gap > 0.15:
                        continue
                    b_gpu = as_float(b, "gpu_median_ms")
                    c_gpu = as_float(c, "gpu_median_ms")
                    b_tri = as_float(b, "triangles_median")
                    c_tri = as_float(c, "triangles_median")
                    out.append({
                        "scene": key[0],
                        "base_distance": key[1],
                        "width": key[2],
                        "height": key[3],
                        "baseline": baseline,
                        "candidate": candidate,
                        "baseline_case": b["case"],
                        "candidate_case": c["case"],
                        "baseline_error": b_error,
                        "candidate_error": c_error,
                        "quality_gap_fraction": relative_gap,
                        "baseline_gpu_ms": b_gpu,
                        "candidate_gpu_ms": c_gpu,
                        "gpu_change_fraction": (c_gpu - b_gpu) / b_gpu if b_gpu > 0 else float("nan"),
                        "baseline_triangles": b_tri,
                        "candidate_triangles": c_tri,
                        "triangle_change_fraction": (c_tri - b_tri) / b_tri if b_tri > 0 else float("nan"),
                        "baseline_boundary_p95_px": as_float(b, "boundary_p95_distance_px"),
                        "candidate_boundary_p95_px": as_float(c, "boundary_p95_distance_px"),
                    })
    return out


def build_calibration(cases: list[dict[str, object]]) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    rows = [r for r in cases if r.get("study") == "estimator-calibration"]
    detail: list[dict[str, object]] = []
    groups: dict[tuple[str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        groups[(str(row["scene"]), str(row["method"]), str(row["base_distance"]))].append(row)
        detail.append({
            "case": row["case"],
            "scene": row["scene"],
            "method": row["method"],
            "distance": row["base_distance"],
            "error_budget_px": row["error_budget_px"],
            "triangles": row["triangles_median"],
            "gpu_ms": row["gpu_median_ms"],
            "image_foreground_rmse": row.get("image_foreground_rmse", ""),
            "image_p95_abs": row.get("image_p95_abs", ""),
            "depth_p95_abs": row.get("depth_p95_abs", ""),
            "boundary_p95_distance_px": row.get("boundary_p95_distance_px", ""),
            "coverage_mismatch_fraction": row.get("coverage_mismatch_fraction", ""),
        })

    summary: list[dict[str, object]] = []
    for (scene, method, distance), group in sorted(groups.items()):
        ordered = sorted(group, key=lambda r: as_float(r, "error_budget_px"))
        metrics = ["image_foreground_rmse", "depth_p95_abs", "boundary_p95_distance_px"]
        result: dict[str, object] = {
            "scene": scene,
            "method": method,
            "distance": distance,
            "points": len(ordered),
        }
        for metric in metrics:
            violations = 0
            comparable = 0
            for a, b in zip(ordered, ordered[1:]):
                av = as_float(a, metric)
                bv = as_float(b, metric)
                if not math.isfinite(av) or not math.isfinite(bv):
                    continue
                comparable += 1
                if bv + 1e-12 < av:
                    violations += 1
            result[f"{metric}_adjacent_violations"] = violations
            result[f"{metric}_comparisons"] = comparable
        summary.append(result)
    return detail, summary


def build_context_gain(cases: list[dict[str, object]]) -> list[dict[str, object]]:
    context = [r for r in cases if r.get("study") == "context-sensitivity"]
    groups: dict[tuple[str, str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in context:
        groups[(
            str(row.get("scene", "")),
            str(row.get("scientific_roughness", "")),
            str(row.get("light_azimuth_degrees", "")),
            str(row.get("normal_budget_degrees", "")),
        )].append(row)

    out: list[dict[str, object]] = []
    for key, rows in groups.items():
        normal_rows = [r for r in rows if r.get("method") == "tessellation-normal-bound"]
        context_rows = [r for r in rows if r.get("method") == "tessellation-context-aware"]
        if not normal_rows:
            continue
        normal = normal_rows[0]
        for candidate in context_rows:
            n_error = as_float(normal, "image_foreground_rmse")
            c_error = as_float(candidate, "image_foreground_rmse")
            n_gpu = as_float(normal, "gpu_median_ms")
            c_gpu = as_float(candidate, "gpu_median_ms")
            n_tri = as_float(normal, "triangles_median")
            c_tri = as_float(candidate, "triangles_median")
            out.append({
                "scene": key[0],
                "scientific_roughness": key[1],
                "light_azimuth_degrees": key[2],
                "normal_budget_degrees": key[3],
                "radiance_budget": candidate.get("radiance_budget", ""),
                "normal_case": normal.get("case", ""),
                "context_case": candidate.get("case", ""),
                "normal_rmse": n_error,
                "context_rmse": c_error,
                "rmse_change_fraction": (c_error - n_error) / n_error if n_error > 0 else float("nan"),
                "normal_gpu_ms": n_gpu,
                "context_gpu_ms": c_gpu,
                "gpu_change_fraction": (c_gpu - n_gpu) / n_gpu if n_gpu > 0 else float("nan"),
                "normal_triangles": n_tri,
                "context_triangles": c_tri,
                "triangle_change_fraction": (c_tri - n_tri) / n_tri if n_tri > 0 else float("nan"),
            })
    return out


def build_scaling_rows(cases: list[dict[str, object]], study: str) -> list[dict[str, object]]:
    fields = [
        "case", "study", "scene", "method", "base_distance", "camera_pitch", "camera_fov_degrees",
        "width", "height", "error_budget_px", "normal_budget_degrees", "radiance_budget",
        "gpu_median_ms", "triangles_median", "image_foreground_rmse", "depth_p95_abs",
        "coverage_mismatch_fraction", "boundary_p95_distance_px",
    ]
    return [
        {key: row.get(key, "") for key in fields}
        for row in cases if row.get("study") == study
    ]


def build_constraint_rows(cases: list[dict[str, object]]) -> list[dict[str, object]]:
    fields = [
        "case", "scene", "method", "error_budget_px", "normal_budget_degrees", "radiance_budget",
        "scientific_roughness", "light_azimuth_degrees", "gpu_median_ms", "triangles_median",
        "image_foreground_rmse", "image_p95_abs", "boundary_p95_distance_px",
    ]
    return [
        {key: row.get(key, "") for key in fields}
        for row in cases if row.get("study") == "constraint-isolation"
    ]


def write_markdown_report(results: Path, cases: list[dict[str, object]], matched: list[dict[str, object]]) -> None:
    context_rows = [r for r in cases if r.get("study") == "context-sensitivity"]
    silhouette_rows = [r for r in cases if r.get("study") == "silhouette-stress"]
    negative_rows = [r for r in cases if r.get("study") == "negative-control"]
    calibration_rows = [r for r in cases if r.get("study") == "estimator-calibration"]
    view_rows = [r for r in cases if r.get("study") == "view-angle-scaling"]
    fov_rows = [r for r in cases if r.get("study") == "fov-scaling"]
    constraint_rows = [r for r in cases if r.get("study") == "constraint-isolation"]
    with (results / "doctoral_report.md").open("w", encoding="utf-8") as f:
        f.write("# Doctoral terrain experiment report\n\n")
        f.write("Generated from raw benchmark CSVs. No values are inserted when the run did not measure them.\n\n")
        f.write(f"- measured cases: {len(cases)}\n")
        f.write(f"- matched-quality pairs within 15% foreground RMSE: {len(matched)}\n")
        f.write(f"- context-sensitivity cases: {len(context_rows)}\n")
        f.write(f"- silhouette-stress cases: {len(silhouette_rows)}\n")
        f.write(f"- negative-control cases: {len(negative_rows)}\n")
        f.write(f"- calibration cases: {len(calibration_rows)}\n")
        f.write(f"- view-angle cases: {len(view_rows)}\n")
        f.write(f"- FOV-scaling cases: {len(fov_rows)}\n")
        f.write(f"- constraint-isolation cases: {len(constraint_rows)}\n\n")
        f.write("## Interpretation gates\n\n")
        f.write("1. Do not claim an error bound from mean RMSE alone. Inspect p95 and silhouette error.\n")
        f.write("2. Compare at matched measured quality, not at equal internal parameter values.\n")
        f.write(
            "3. Context-conditioned refinement is useful only if gains concentrate "
            "in glossy/light-sensitive cases.\n"
        )
        f.write("4. Smooth-hills and badlands are retained as negative controls; losses must be reported.\n")
        f.write(
            "5. A method is not a contribution merely because it uses hardware tessellation "
            "or screen-space error.\n"
        )
        f.write("6. Treat the radiometric constraint as supported only if radiance sweeps show a selective "
                "quality gain relative to the normal-bound control.\n")
        f.write("7. Projection laws are sanity checks: narrower FOV and lower camera pitch should increase "
                "geometric demand in the controlled cases.\n")


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze gfx-research-terrain doctoral benchmark output")
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

    all_fields = list(summary_fields)
    for row in quality:
        for key in row:
            if key not in all_fields:
                all_fields.append(key)
    write_csv(args.results / "cases.csv", cases, all_fields)

    baseline_cases = [
        row for row in cases
        if row.get("study") == "strong-baseline" and row.get("image_foreground_rmse", "") != ""
    ]
    pareto_rows: list[dict[str, object]] = []
    grouped: dict[tuple[str, str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in baseline_cases:
        grouped[(
            str(row["scene"]), str(row["base_distance"]), str(row["width"]), str(row["height"])
        )].append(row)

    for group_rows in grouped.values():
        triangle_flags = pareto_flags(group_rows, "triangles_median", "image_foreground_rmse")
        gpu_flags = pareto_flags(group_rows, "gpu_median_ms", "image_foreground_rmse")
        silhouette_flags = pareto_flags(group_rows, "gpu_median_ms", "boundary_p95_distance_px")
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
                "image_foreground_rmse": row.get("image_foreground_rmse", ""),
                "boundary_p95_distance_px": row.get("boundary_p95_distance_px", ""),
                "pareto_triangles_vs_image": int(triangle_flags[str(row["case"])]),
                "pareto_gpu_vs_image": int(gpu_flags[str(row["case"])]),
                "pareto_gpu_vs_silhouette": int(silhouette_flags[str(row["case"])]),
            })
    pareto_rows.sort(key=lambda row: int(str(row["case"])))
    pareto_fields = list(pareto_rows[0].keys()) if pareto_rows else ["case"]
    write_csv(args.results / "pareto.csv", pareto_rows, pareto_fields)

    matched = build_matched_quality(cases)
    matched_fields = list(matched[0].keys()) if matched else ["scene"]
    write_csv(args.results / "matched_quality.csv", matched, matched_fields)

    calibration, calibration_summary = build_calibration(cases)
    write_csv(
        args.results / "calibration.csv",
        calibration,
        list(calibration[0].keys()) if calibration else ["case"],
    )
    write_csv(
        args.results / "calibration_summary.csv",
        calibration_summary,
        list(calibration_summary[0].keys()) if calibration_summary else ["scene"],
    )

    context = [r for r in cases if r.get("study") == "context-sensitivity"]
    context_fields = [
        "case", "scene", "method", "normal_budget_degrees", "radiance_budget",
        "scientific_roughness", "light_azimuth_degrees", "light_elevation_degrees",
        "gpu_median_ms", "triangles_median", "image_foreground_rmse", "image_p95_abs",
        "boundary_p95_distance_px",
    ]
    write_csv(
        args.results / "context_sensitivity.csv",
        [{key: row.get(key, "") for key in context_fields} for row in context],
        context_fields,
    )

    silhouette = [r for r in cases if r.get("study") == "silhouette-stress"]
    silhouette_fields = [
        "case", "scene", "method", "camera_pitch", "gpu_median_ms", "triangles_median",
        "coverage_mismatch_fraction", "boundary_mean_distance_px", "boundary_p95_distance_px",
        "boundary_max_distance_px", "image_foreground_rmse", "depth_p95_abs",
    ]
    write_csv(
        args.results / "silhouette.csv",
        [{key: row.get(key, "") for key in silhouette_fields} for row in silhouette],
        silhouette_fields,
    )

    negative = [r for r in cases if r.get("study") == "negative-control"]
    negative_fields = [
        "case", "scene", "method", "gpu_median_ms", "triangles_median", "image_foreground_rmse",
        "boundary_p95_distance_px",
    ]
    write_csv(
        args.results / "negative_controls.csv",
        [{key: row.get(key, "") for key in negative_fields} for row in negative],
        negative_fields,
    )

    temporal = [r for r in cases if r.get("study") == "temporal-stability"]
    temporal_fields = [
        "case", "scene", "method", "hysteresis", "morph_band", "motion_amplitude",
        "transitions_total", "transitions_mean", "gpu_median_ms", "adaptation_cpu_median_ms",
        "image_foreground_rmse",
    ]
    write_csv(
        args.results / "temporal.csv",
        [{key: row.get(key, "") for key in temporal_fields} for row in temporal],
        temporal_fields,
    )

    context_gain = build_context_gain(cases)
    write_csv(
        args.results / "context_gain.csv",
        context_gain,
        list(context_gain[0].keys()) if context_gain else ["scene"],
    )

    view_angle = build_scaling_rows(cases, "view-angle-scaling")
    scaling_fields = list(view_angle[0].keys()) if view_angle else ["case"]
    write_csv(args.results / "view_angle.csv", view_angle, scaling_fields)

    fov_scaling = build_scaling_rows(cases, "fov-scaling")
    fov_fields = list(fov_scaling[0].keys()) if fov_scaling else ["case"]
    write_csv(args.results / "fov_scaling.csv", fov_scaling, fov_fields)

    resolution_scaling = build_scaling_rows(cases, "resolution-scaling")
    resolution_fields = list(resolution_scaling[0].keys()) if resolution_scaling else ["case"]
    write_csv(args.results / "resolution_scaling.csv", resolution_scaling, resolution_fields)

    constraints = build_constraint_rows(cases)
    write_csv(
        args.results / "constraint_sweeps.csv",
        constraints,
        list(constraints[0].keys()) if constraints else ["case"],
    )

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

    write_markdown_report(args.results, cases, matched)

    for name in [
        "summary.csv",
        "cases.csv",
        "pareto.csv",
        "matched_quality.csv",
        "calibration.csv",
        "calibration_summary.csv",
        "context_sensitivity.csv",
        "context_gain.csv",
        "view_angle.csv",
        "fov_scaling.csv",
        "resolution_scaling.csv",
        "constraint_sweeps.csv",
        "silhouette.csv",
        "negative_controls.csv",
        "temporal.csv",
        "null.csv",
        "doctoral_report.md",
    ]:
        print(f"wrote {args.results / name}")

    if null_failed:
        raise SystemExit("NULL TEST FAILED: do not use this run for publication claims")


if __name__ == "__main__":
    main()
