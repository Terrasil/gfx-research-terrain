from __future__ import annotations

import argparse
import csv
import math
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Check:
    name: str
    passed: bool
    detail: str
    critical: bool = True


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def as_float(row: dict[str, str], key: str, default: float = float("nan")) -> float:
    try:
        return float(row.get(key, ""))
    except (TypeError, ValueError):
        return default


def median(values: list[float]) -> float:
    xs = sorted(values)
    if not xs:
        return float("nan")
    n = len(xs)
    if n % 2:
        return xs[n // 2]
    return 0.5 * (xs[n // 2 - 1] + xs[n // 2])


def group_raw(raw: list[dict[str, str]]) -> dict[str, list[dict[str, str]]]:
    out: dict[str, list[dict[str, str]]] = {}
    for row in raw:
        out.setdefault(row["case"], []).append(row)
    return out


def case_summary(rows: list[dict[str, str]]) -> dict[str, object]:
    first = rows[0]
    return {
        "case": first["case"],
        "study": first["study"],
        "scene": first["scene"],
        "method": first["method"],
        "base_distance": as_float(first, "base_distance"),
        "error_budget_px": as_float(first, "error_budget_px"),
        "normal_budget_degrees": as_float(first, "normal_budget_degrees"),
        "radiance_budget": as_float(first, "radiance_budget"),
        "scientific_roughness": as_float(first, "scientific_roughness"),
        "camera_pitch": as_float(first, "camera_pitch"),
        "camera_fov_degrees": as_float(first, "camera_fov_degrees", 55.0),
        "width": int(float(first["width"])),
        "height": int(float(first["height"])),
        "triangles": median([as_float(r, "triangles") for r in rows]),
        "gpu_ms": median([as_float(r, "gpu_ms") for r in rows]),
        "adaptation_cpu_ms": median([as_float(r, "adaptation_cpu_ms", 0.0) for r in rows]),
        "topology_open": max(as_float(r, "topology_open_internal_edges", 0.0) for r in rows),
        "topology_nonmanifold": max(as_float(r, "topology_nonmanifold_edges", 0.0) for r in rows),
    }


def nonincreasing(values: list[float], tolerance: float = 1e-9) -> bool:
    return all(b <= a + tolerance for a, b in zip(values, values[1:]))


def nondecreasing(values: list[float], tolerance: float = 1e-9) -> bool:
    return all(b + tolerance >= a for a, b in zip(values, values[1:]))


def run_checks(results: Path) -> list[Check]:
    raw_path = results / "raw.csv"
    quality_path = results / "quality.csv"
    if not raw_path.exists() or not quality_path.exists():
        return [Check("required files", False, "raw.csv or quality.csv is missing")]

    raw = read_rows(raw_path)
    quality = read_rows(quality_path)
    checks: list[Check] = []
    checks.append(Check("raw samples", bool(raw), f"rows={len(raw)}"))
    checks.append(Check("quality samples", bool(quality), f"rows={len(quality)}"))
    if not raw:
        return checks

    bad_numeric = 0
    for row in raw:
        for key in ("gpu_ms", "triangles", "adaptation_cpu_ms"):
            value = as_float(row, key)
            if not math.isfinite(value) or value < 0.0:
                bad_numeric += 1
    checks.append(Check("finite non-negative runtime metrics", bad_numeric == 0, f"bad_values={bad_numeric}"))

    grouped = group_raw(raw)
    summaries = [case_summary(rows) for rows in grouped.values()]

    null_rows = [r for r in quality if r.get("study") == "null-equivalence"]
    null_fail = [r for r in null_rows if r.get("rgba_exact") != "1" or r.get("depth_exact") != "1"]
    checks.append(Check(
        "byte-exact null equivalence",
        len(null_rows) >= 4 and not null_fail,
        f"cases={len(null_rows)}, failed={len(null_fail)}",
    ))

    adaptive = [
        r for r in summaries
        if r["study"] == "harness-validation" and r["method"] == "adaptive-error-feature"
    ]
    topology_fail = [
        r for r in adaptive
        if float(r["topology_open"]) != 0.0 or float(r["topology_nonmanifold"]) != 0.0
    ]
    checks.append(Check(
        "adaptive mesh topology",
        len(adaptive) >= 4 and not topology_fail,
        "checked=%d, failed=%d%s" % (
            len(adaptive), len(topology_fail),
            "" if not topology_fail else f", failing_cases={[r['case'] for r in topology_fail]}",
        ),
    ))

    budget_rows = sorted([
        r for r in summaries
        if r["study"] == "harness-validation"
        and r["scene"] == "sharp-ridge"
        and r["method"] == "tessellation-error"
        and r["width"] == 960 and r["height"] == 540
        and math.isclose(float(r["base_distance"]), 10.0, abs_tol=1e-5)
    ], key=lambda r: float(r["error_budget_px"]))
    budget_triangles = [float(r["triangles"]) for r in budget_rows]
    checks.append(Check(
        "budget monotonicity",
        len(budget_rows) == 3 and nonincreasing(budget_triangles),
        f"budgets={[r['error_budget_px'] for r in budget_rows]}, triangles={budget_triangles}",
    ))

    resolution_rows = sorted([
        r for r in summaries
        if r["study"] == "harness-validation"
        and r["scene"] == "mixed-frequency"
        and r["method"] == "tessellation-error"
        and math.isclose(float(r["base_distance"]), 11.0, abs_tol=1e-5)
    ], key=lambda r: int(r["height"]))
    resolution_triangles = [float(r["triangles"]) for r in resolution_rows]
    checks.append(Check(
        "resolution law",
        len(resolution_rows) == 3 and nondecreasing(resolution_triangles),
        f"heights={[r['height'] for r in resolution_rows]}, triangles={resolution_triangles}",
    ))

    distance_rows = sorted([
        r for r in summaries
        if r["study"] == "harness-validation"
        and r["scene"] == "mixed-frequency"
        and r["method"] == "tessellation-error"
        and r["width"] == 960 and r["height"] == 540
        and math.isclose(float(r["error_budget_px"]), 1.0, abs_tol=1e-5)
    ], key=lambda r: float(r["base_distance"]))
    # Remove the resolution-law 11.0 m case; distance law explicitly uses 6/12/24 m.
    distance_rows = [r for r in distance_rows if any(
        math.isclose(float(r["base_distance"]), d, abs_tol=1e-5) for d in (6.0, 12.0, 24.0)
    )]
    distance_triangles = [float(r["triangles"]) for r in distance_rows]
    checks.append(Check(
        "distance law",
        len(distance_rows) == 3 and nonincreasing(distance_triangles),
        f"distances={[r['base_distance'] for r in distance_rows]}, triangles={distance_triangles}",
    ))

    fov_rows = sorted([
        r for r in summaries
        if r["study"] == "fov-scaling"
        and r["scene"] == "mixed-frequency"
        and r["method"] == "tessellation-error"
        and r["width"] == 960 and r["height"] == 540
    ], key=lambda r: float(r["camera_fov_degrees"]))
    fov_triangles = [float(r["triangles"]) for r in fov_rows]
    checks.append(Check(
        "FOV projection law",
        len(fov_rows) == 3 and nonincreasing(fov_triangles),
        f"fov={[r['camera_fov_degrees'] for r in fov_rows]}, triangles={fov_triangles}",
    ))

    angle_rows = sorted([
        r for r in summaries
        if r["study"] == "view-angle-scaling"
        and r["scene"] == "smooth-hills"
        and r["method"] == "tessellation-error"
        and r["width"] == 960 and r["height"] == 540
    ], key=lambda r: float(r["camera_pitch"]))
    angle_triangles = [float(r["triangles"]) for r in angle_rows]
    angle_ok = len(angle_rows) == 2 and angle_triangles[0] + 1e-9 >= angle_triangles[1]
    checks.append(Check(
        "view-angle projection law",
        angle_ok,
        f"pitch={[r['camera_pitch'] for r in angle_rows]}, triangles={angle_triangles}",
    ))

    normal_rows = sorted([
        r for r in summaries
        if r["study"] == "constraint-isolation"
        and r["scene"] == "sharp-ridge"
        and r["method"] == "tessellation-normal-bound"
        and float(r["radiance_budget"]) < 1000.0
    ], key=lambda r: float(r["normal_budget_degrees"]))
    normal_triangles = [float(r["triangles"]) for r in normal_rows]
    checks.append(Check(
        "normal-budget monotonicity",
        len(normal_rows) == 3 and nonincreasing(normal_triangles),
        f"normal_budget={[r['normal_budget_degrees'] for r in normal_rows]}, triangles={normal_triangles}",
    ))

    radiance_rows = sorted([
        r for r in summaries
        if r["study"] == "constraint-isolation"
        and r["scene"] == "sharp-ridge"
        and r["method"] == "tessellation-context-aware"
        and math.isclose(float(r["normal_budget_degrees"]), 30.0, abs_tol=1e-5)
        and float(r["radiance_budget"]) < 1.0
    ], key=lambda r: float(r["radiance_budget"]))
    radiance_triangles = [float(r["triangles"]) for r in radiance_rows]
    checks.append(Check(
        "radiance-budget monotonicity",
        len(radiance_rows) == 3 and nonincreasing(radiance_triangles),
        f"radiance_budget={[r['radiance_budget'] for r in radiance_rows]}, triangles={radiance_triangles}",
    ))

    collapse_normal = [
        r for r in summaries
        if r["study"] == "constraint-isolation"
        and r["scene"] == "sharp-ridge"
        and math.isclose(float(r["radiance_budget"]), 1000000.0, rel_tol=0.0, abs_tol=1.0)
        and math.isclose(float(r["normal_budget_degrees"]), 5.0, abs_tol=1e-5)
    ]
    collapse_normal_map = {str(r["method"]): float(r["triangles"]) for r in collapse_normal}
    normal_equal = (
        "tessellation-normal-bound" in collapse_normal_map
        and "tessellation-context-aware" in collapse_normal_map
        and math.isclose(
            collapse_normal_map["tessellation-normal-bound"],
            collapse_normal_map["tessellation-context-aware"],
            rel_tol=0.0,
            abs_tol=0.5,
        )
    )
    checks.append(Check(
        "context radiance-null equivalence",
        normal_equal,
        f"triangles={collapse_normal_map}",
    ))

    collapse_geometry = [
        r for r in summaries
        if r["study"] == "constraint-isolation"
        and r["scene"] == "mixed-frequency"
        and float(r["normal_budget_degrees"]) > 100000.0
        and float(r["radiance_budget"]) > 100000.0
    ]
    collapse_geometry_map = {str(r["method"]): float(r["triangles"]) for r in collapse_geometry}
    geometry_equal = (
        "tessellation-error" in collapse_geometry_map
        and "tessellation-context-aware" in collapse_geometry_map
        and math.isclose(
            collapse_geometry_map["tessellation-error"],
            collapse_geometry_map["tessellation-context-aware"],
            rel_tol=0.0,
            abs_tol=0.5,
        )
    )
    checks.append(Check(
        "context full-null equivalence",
        geometry_equal,
        f"triangles={collapse_geometry_map}",
    ))

    expected_methods = {
        "tessellation-distance",
        "tessellation-variance-baseline",
        "tessellation-error",
        "tessellation-normal-bound",
        "tessellation-context-aware",
    }
    smoke_methods = {
        str(r["method"]) for r in summaries
        if r["study"] == "harness-validation" and r["scene"] == "glacial-valley"
    }
    missing = sorted(expected_methods - smoke_methods)
    checks.append(Check("doctoral method smoke coverage", not missing, f"missing={missing}"))

    gpu_zero = [r for r in summaries if not math.isfinite(float(r["gpu_ms"])) or float(r["gpu_ms"]) <= 0.0]
    checks.append(Check(
        "GPU timer produced positive samples",
        not gpu_zero,
        f"failed_cases={[r['case'] for r in gpu_zero]}",
    ))

    adaptive_cpu = [float(r["adaptation_cpu_ms"]) for r in adaptive if math.isfinite(float(r["adaptation_cpu_ms"]))]
    cache_ok = bool(adaptive_cpu) and max(adaptive_cpu) < 1.0
    checks.append(Check(
        "steady-state adaptive cache",
        cache_ok,
        f"median_case_costs_ms={adaptive_cpu}; target < 1.0 ms",
        critical=False,
    ))
    return checks


def write_report(results: Path, checks: list[Check]) -> None:
    failures = [c for c in checks if c.critical and not c.passed]
    warnings = [c for c in checks if not c.critical and not c.passed]
    with (results / "validation_report.md").open("w", encoding="utf-8") as f:
        f.write("# Automated validation report\n\n")
        f.write(f"Overall: **{'FAIL' if failures else 'PASS'}**\n\n")
        f.write(f"Critical failures: {len(failures)}  \nWarnings: {len(warnings)}\n\n")
        f.write("| Check | Status | Severity | Detail |\n")
        f.write("|---|---|---|---|\n")
        for check in checks:
            status = "PASS" if check.passed else ("FAIL" if check.critical else "WARN")
            severity = "critical" if check.critical else "warning"
            detail = check.detail.replace("|", "\\|")
            f.write(f"| {check.name} | {status} | {severity} | {detail} |\n")
        f.write("\nA failed critical gate means the run must not be used for publication or thesis claims.\n")


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate gfx-research-terrain automated GPU test output")
    parser.add_argument("results", type=Path)
    args = parser.parse_args()

    checks = run_checks(args.results)
    write_report(args.results, checks)
    for check in checks:
        status = "PASS" if check.passed else ("FAIL" if check.critical else "WARN")
        print(f"[{status}] {check.name}: {check.detail}")

    failures = [c for c in checks if c.critical and not c.passed]
    if failures:
        raise SystemExit(f"validation failed: {len(failures)} critical gate(s)")
    print(f"validation passed: {args.results / 'validation_report.md'}")


if __name__ == "__main__":
    main()
