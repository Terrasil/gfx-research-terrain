#include <gfx/terrain/Metrics.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace gfx::terrain {

ImageMetrics compare_rgba(
    std::span<const float> reference,
    std::span<const float> candidate,
    std::span<const float> reference_depth,
    std::span<const float> candidate_depth) {

    ImageMetrics metrics;
    const std::size_t count = std::min(reference.size(), candidate.size());
    if (count < 4) return metrics;

    const bool have_depth =
        reference_depth.size() >= count / 4 && candidate_depth.size() >= count / 4;

    double abs_sum = 0.0;
    double sq_sum = 0.0;
    double ref_sq_sum = 0.0;
    double foreground_sq_sum = 0.0;
    std::size_t channels = 0;
    std::size_t foreground_channels = 0;
    double peak = 1.0;

    for (std::size_t i = 0, pixel = 0; i + 3 < count; i += 4, ++pixel) {
        const bool foreground = !have_depth ||
            reference_depth[pixel] < 1.0f || candidate_depth[pixel] < 1.0f;

        for (std::size_t c = 0; c < 3; ++c) {
            const double r = static_cast<double>(reference[i + c]);
            const double d = static_cast<double>(candidate[i + c]) - r;
            const double a = std::abs(d);
            abs_sum += a;
            sq_sum += d * d;
            ref_sq_sum += r * r;
            metrics.max_abs_error = std::max(metrics.max_abs_error, a);
            peak = std::max(peak, std::abs(r));
            ++channels;

            if (foreground) {
                foreground_sq_sum += d * d;
                ++foreground_channels;
            }
        }
    }

    if (channels == 0) return metrics;
    metrics.mae = abs_sum / static_cast<double>(channels);
    metrics.rmse = std::sqrt(sq_sum / static_cast<double>(channels));
    metrics.relative_rmse = ref_sq_sum > 0.0 ? std::sqrt(sq_sum / ref_sq_sum) : 0.0;
    metrics.foreground_rmse = foreground_channels > 0
        ? std::sqrt(foreground_sq_sum / static_cast<double>(foreground_channels))
        : 0.0;
    metrics.psnr = metrics.rmse > 0.0
        ? 20.0 * std::log10(peak / metrics.rmse)
        : std::numeric_limits<double>::infinity();
    return metrics;
}

DepthMetrics compare_depth(std::span<const float> reference, std::span<const float> candidate) {
    DepthMetrics metrics;
    const std::size_t count = std::min(reference.size(), candidate.size());
    if (count == 0) return metrics;

    double abs_sum = 0.0;
    double sq_sum = 0.0;
    std::size_t measured = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (reference[i] >= 1.0f && candidate[i] >= 1.0f) continue;
        const double d = static_cast<double>(candidate[i]) - static_cast<double>(reference[i]);
        const double a = std::abs(d);
        abs_sum += a;
        sq_sum += d * d;
        metrics.max_abs_error = std::max(metrics.max_abs_error, a);
        ++measured;
    }
    if (measured == 0) return metrics;
    metrics.mae = abs_sum / static_cast<double>(measured);
    metrics.rmse = std::sqrt(sq_sum / static_cast<double>(measured));
    return metrics;
}

EqualityMetrics compare_exact(
    std::span<const float> reference_rgba,
    std::span<const float> candidate_rgba,
    std::span<const float> reference_depth,
    std::span<const float> candidate_depth) {

    EqualityMetrics result;
    result.rgba_exact = reference_rgba.size() == candidate_rgba.size() &&
        (reference_rgba.empty() || std::memcmp(
            reference_rgba.data(), candidate_rgba.data(), reference_rgba.size_bytes()) == 0);
    result.depth_exact = reference_depth.size() == candidate_depth.size() &&
        (reference_depth.empty() || std::memcmp(
            reference_depth.data(), candidate_depth.data(), reference_depth.size_bytes()) == 0);
    return result;
}

}
