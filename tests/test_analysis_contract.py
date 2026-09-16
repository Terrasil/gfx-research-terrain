from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("analyze_results", ROOT / "tools" / "analyze_results.py")
assert spec and spec.loader
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


def test_percentile_endpoints() -> None:
    values = [1.0, 2.0, 3.0, 4.0]
    assert analysis.percentile(values, 0.0) == 1.0
    assert analysis.percentile(values, 1.0) == 4.0


def test_pareto_marks_dominated_point() -> None:
    rows = [
        {"case": "1", "cost": 1.0, "error": 1.0},
        {"case": "2", "cost": 2.0, "error": 2.0},
        {"case": "3", "cost": 0.8, "error": 1.5},
    ]
    flags = analysis.pareto_flags(rows, "cost", "error")
    assert flags["1"]
    assert not flags["2"]
    assert flags["3"]


def test_matched_quality_rejects_large_quality_gap() -> None:
    rows = [
        {
            "case": "1", "study": "strong-baseline", "scene": "x", "base_distance": "10",
            "width": "1280", "height": "720", "method": "tessellation-distance",
            "image_foreground_rmse": "0.10", "gpu_median_ms": 1.0, "triangles_median": 1000,
        },
        {
            "case": "2", "study": "strong-baseline", "scene": "x", "base_distance": "10",
            "width": "1280", "height": "720", "method": "tessellation-error",
            "image_foreground_rmse": "0.20", "gpu_median_ms": 0.8, "triangles_median": 800,
        },
    ]
    assert analysis.build_matched_quality(rows) == []


def test_context_gain_pairs_context_with_normal_control() -> None:
    rows = [
        {
            "case": "1", "study": "context-sensitivity", "scene": "ridge",
            "method": "tessellation-normal-bound", "scientific_roughness": "0.2",
            "light_azimuth_degrees": "0", "normal_budget_degrees": "20",
            "image_foreground_rmse": "0.1", "gpu_median_ms": 1.0, "triangles_median": 1000,
        },
        {
            "case": "2", "study": "context-sensitivity", "scene": "ridge",
            "method": "tessellation-context-aware", "scientific_roughness": "0.2",
            "light_azimuth_degrees": "0", "normal_budget_degrees": "20", "radiance_budget": "0.02",
            "image_foreground_rmse": "0.08", "gpu_median_ms": 1.1, "triangles_median": 1100,
        },
    ]
    pairs = analysis.build_context_gain(rows)
    assert len(pairs) == 1
    assert pairs[0]["rmse_change_fraction"] < 0.0
    assert pairs[0]["gpu_change_fraction"] > 0.0
