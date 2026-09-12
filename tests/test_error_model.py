from __future__ import annotations

import math


def projected_error_px(world_error: float, nearest_distance: float, viewport_height: int, fov_degrees: float) -> float:
    focal = viewport_height / (2.0 * math.tan(math.radians(fov_degrees) * 0.5))
    return world_error * focal / nearest_distance


def feature_error_px(
    world_error: float,
    radius: float,
    normal_degrees: float,
    feature: float,
    weight: float,
    nearest_distance: float,
    viewport_height: int,
    fov_degrees: float,
) -> float:
    geom = projected_error_px(world_error, nearest_distance, viewport_height, fov_degrees)
    focal = viewport_height / (2.0 * math.tan(math.radians(fov_degrees) * 0.5))
    normal_proxy = radius * math.sin(math.radians(min(normal_degrees, 90.0))) * focal / nearest_distance
    return geom + weight * (0.25 * feature * geom + 0.05 * normal_proxy)


def test_projected_error_decreases_with_distance() -> None:
    near = projected_error_px(0.1, 5.0, 1080, 60.0)
    far = projected_error_px(0.1, 20.0, 1080, 60.0)
    assert near > far


def test_projected_error_scales_with_resolution() -> None:
    low = projected_error_px(0.1, 10.0, 720, 60.0)
    high = projected_error_px(0.1, 10.0, 1440, 60.0)
    assert math.isclose(high, 2.0 * low, rel_tol=1e-12)


def test_projected_error_scales_with_world_residual() -> None:
    a = projected_error_px(0.05, 10.0, 1080, 60.0)
    b = projected_error_px(0.10, 10.0, 1080, 60.0)
    assert math.isclose(b, 2.0 * a, rel_tol=1e-12)


def test_feature_term_never_reduces_geometric_estimate() -> None:
    geom = projected_error_px(0.1, 8.0, 1080, 55.0)
    feature = feature_error_px(0.1, 1.5, 20.0, 0.8, 1.0, 8.0, 1080, 55.0)
    assert feature >= geom


def test_zero_feature_weight_is_exact_geometric_estimator() -> None:
    geom = projected_error_px(0.1, 8.0, 1080, 55.0)
    feature = feature_error_px(0.1, 1.5, 20.0, 0.8, 0.0, 8.0, 1080, 55.0)
    assert math.isclose(feature, geom, rel_tol=1e-12)
