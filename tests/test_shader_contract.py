from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
TESC = (ROOT / "shaders" / "tess_surface.tesc").read_text(encoding="utf-8")
TESE = (ROOT / "shaders" / "tess_surface.tese").read_text(encoding="utf-8")
VERT = (ROOT / "shaders" / "tess_surface.vert").read_text(encoding="utf-8")


def test_tessellation_uses_fractional_spacing() -> None:
    assert "fractional_even_spacing" in TESE


def test_tessellation_winding_matches_heightfield_orientation() -> None:
    assert re.search(r"layout\s*\(\s*quads\s*,[^)]*\bcw\b", TESE)


def test_patch_keyword_is_not_used_as_plain_identifier() -> None:
    assert not re.search(r"\b(?:int|float|vec[234]|mat[234])\s+patch\b", VERT + "\n" + TESC + "\n" + TESE)


def test_outer_factors_are_evaluated_from_shared_edge_endpoints() -> None:
    required = [
        "controlledEdgeFactor(p0, p3)",
        "controlledEdgeFactor(p0, p1)",
        "controlledEdgeFactor(p1, p2)",
        "controlledEdgeFactor(p3, p2)",
    ]
    assert all(text in TESC for text in required)


def test_multi_channel_context_method_uses_max_not_sum() -> None:
    compact = " ".join(TESC.split())
    assert "return max(g, max(normalBoundRatio(a, b), contextRadianceRatio(a, b)))" in compact


def test_geometric_projection_uses_actual_screen_displacement() -> None:
    compact = " ".join(TESC.split())
    assert "uniform mat4 uMvp" in compact
    assert "uniform int uViewportWidth" in compact
    assert "plusClip = uMvp * vec4(worldPoint + vec3(0.0, errorMagnitude, 0.0), 1.0)" in compact
    assert "minusClip = uMvp * vec4(worldPoint - vec3(0.0, errorMagnitude, 0.0), 1.0)" in compact
    assert "return max(plusPixels, minusPixels)" in compact
