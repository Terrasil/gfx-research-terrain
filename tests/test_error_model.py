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
    normal_scale = max(0.0, min(1.0, geom / max(geom + 1.0, 1.0)))
    return geom + weight * (0.25 * feature * geom + 0.05 * normal_proxy * normal_scale)


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


def adaptive_split(ratio: float, was_split: bool, hysteresis: float) -> bool:
    threshold = 1.0 - hysteresis if was_split else 1.0 + hysteresis
    return ratio > threshold


def smoothstep01(value: float) -> float:
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def morph_factor(parent_ratio: float, hysteresis: float, morph_band: float) -> float:
    enter = 1.0 + hysteresis
    return smoothstep01((parent_ratio - enter) / max(morph_band, 0.01))


def test_hysteresis_has_distinct_split_and_merge_thresholds() -> None:
    assert not adaptive_split(1.10, False, 0.15)
    assert adaptive_split(1.20, False, 0.15)
    assert adaptive_split(0.90, True, 0.15)
    assert not adaptive_split(0.80, True, 0.15)


def test_geomorph_starts_on_parent_surface_at_split_threshold() -> None:
    assert math.isclose(morph_factor(1.15, 0.15, 0.75), 0.0, abs_tol=1e-12)
    assert 0.0 < morph_factor(1.40, 0.15, 0.75) < 1.0
    assert math.isclose(morph_factor(1.90, 0.15, 0.75), 1.0, abs_tol=1e-12)


def test_feature_estimator_collapses_to_zero_for_exact_reference_geometry() -> None:
    feature = feature_error_px(0.0, 1.5, 45.0, 1.0, 3.0, 4.0, 2160, 45.0)
    assert math.isclose(feature, 0.0, abs_tol=1e-12)


def multi_channel_ratio(geom_ratio: float, normal_ratio: float, radiance_ratio: float) -> float:
    return max(geom_ratio, normal_ratio, radiance_ratio)


def test_multi_channel_controller_never_relaxes_geometric_constraint() -> None:
    assert multi_channel_ratio(1.25, 0.2, 0.3) == 1.25


def test_context_channel_is_an_incremental_constraint() -> None:
    base = multi_channel_ratio(0.6, 0.8, 0.0)
    insensitive = multi_channel_ratio(0.6, 0.8, 0.1)
    sensitive = multi_channel_ratio(0.6, 0.8, 1.4)
    assert insensitive == base
    assert sensitive > base


def geometric_tess_factor(ratio: float) -> float:
    return max(1.0, 1.15 * math.sqrt(max(ratio, 0.0)))


def first_order_tess_factor(ratio: float) -> float:
    return max(1.0, 1.05 * max(ratio, 0.0))


def test_normal_constraint_is_not_weakened_by_geometric_square_root_scaling() -> None:
    ratio = 4.0
    assert first_order_tess_factor(ratio) > geometric_tess_factor(ratio)


def directional_height_screen_proxy(world_error: float, view_direction_y: float) -> float:
    view_direction_y = max(-1.0, min(1.0, view_direction_y))
    transverse = math.sqrt(max(0.0, 1.0 - view_direction_y * view_direction_y))
    return world_error * transverse


def test_narrower_fov_increases_projected_error() -> None:
    narrow = projected_error_px(0.1, 10.0, 1080, 35.0)
    wide = projected_error_px(0.1, 10.0, 1080, 80.0)
    assert narrow > wide


def test_height_residual_is_less_visible_when_view_aligned() -> None:
    near_top_down = directional_height_screen_proxy(0.1, 0.98)
    grazing = directional_height_screen_proxy(0.1, 0.10)
    assert near_top_down < grazing


def test_tightening_normal_budget_cannot_reduce_constraint_ratio() -> None:
    normal_error = 8.0
    loose = multi_channel_ratio(0.6, normal_error / 10.0, 0.0)
    tight = multi_channel_ratio(0.6, normal_error / 5.0, 0.0)
    assert tight >= loose


def test_disabling_extra_channels_collapses_to_geometry_ratio() -> None:
    geometry = 0.85
    ratio = multi_channel_ratio(geometry, 8.0 / 1_000_000.0, 0.02 / 1_000_000.0)
    assert math.isclose(ratio, geometry, rel_tol=0.0, abs_tol=1e-12)
