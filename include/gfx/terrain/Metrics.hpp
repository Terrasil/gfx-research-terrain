#pragma once

#include <span>

namespace gfx::terrain {

struct ImageMetrics {
    double mae = 0.0;
    double rmse = 0.0;
    double relative_rmse = 0.0;
    double foreground_rmse = 0.0;
    double psnr = 0.0;
    double max_abs_error = 0.0;
    double p95_abs_error = 0.0;
};

struct DepthMetrics {
    double mae = 0.0;
    double rmse = 0.0;
    double max_abs_error = 0.0;
    double p95_abs_error = 0.0;
};

struct SilhouetteMetrics {
    double coverage_mismatch_fraction = 0.0;
    double boundary_mean_distance_px = 0.0;
    double boundary_p95_distance_px = 0.0;
    double boundary_max_distance_px = 0.0;
};

struct EqualityMetrics {
    bool rgba_exact = false;
    bool depth_exact = false;
};

[[nodiscard]] ImageMetrics compare_rgba(
    std::span<const float> reference,
    std::span<const float> candidate,
    std::span<const float> reference_depth = {},
    std::span<const float> candidate_depth = {});

[[nodiscard]] DepthMetrics compare_depth(
    std::span<const float> reference,
    std::span<const float> candidate);

[[nodiscard]] SilhouetteMetrics compare_silhouette(
    std::span<const float> reference_depth,
    std::span<const float> candidate_depth,
    int width,
    int height);

[[nodiscard]] EqualityMetrics compare_exact(
    std::span<const float> reference_rgba,
    std::span<const float> candidate_rgba,
    std::span<const float> reference_depth,
    std::span<const float> candidate_depth);

}
