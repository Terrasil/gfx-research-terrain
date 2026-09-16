#include <gfx/terrain/Metrics.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace gfx::terrain {

namespace {

double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(position));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(position));
    if (lo == hi) return values[lo];
    const double t = position - static_cast<double>(lo);
    return values[lo] * (1.0 - t) + values[hi] * t;
}

std::vector<unsigned char> build_foreground_mask(std::span<const float> depth, std::size_t count) {
    std::vector<unsigned char> mask(count, 0);
    const std::size_t usable = std::min(count, depth.size());
    for (std::size_t i = 0; i < usable; ++i) mask[i] = depth[i] < 1.0f - 1.0e-7f ? 1 : 0;
    return mask;
}

std::vector<unsigned char> build_boundary_mask(
    const std::vector<unsigned char>& foreground,
    int width,
    int height) {

    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<unsigned char> boundary(count, 0);
    if (foreground.size() < count || width <= 0 || height <= 0) return boundary;

    const auto value = [&](int x, int y) {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return foreground[static_cast<std::size_t>(y * width + x)];
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char center = value(x, y);
            if (value(x - 1, y) != center || value(x + 1, y) != center
                || value(x, y - 1) != center || value(x, y + 1) != center) {
                boundary[static_cast<std::size_t>(y * width + x)] = 1;
            }
        }
    }
    return boundary;
}

std::vector<float> chamfer_distance(const std::vector<unsigned char>& boundary, int width, int height) {
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    constexpr float inf = 1.0e9f;
    constexpr float diagonal = 1.41421356237f;
    std::vector<float> distance(count, inf);
    if (boundary.size() < count || width <= 0 || height <= 0) return distance;

    for (std::size_t i = 0; i < count; ++i) {
        if (boundary[i] != 0) distance[i] = 0.0f;
    }

    const auto update = [&](int x, int y, int nx, int ny, float weight, std::vector<float>& values) {
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) return;
        const std::size_t index = static_cast<std::size_t>(y * width + x);
        const std::size_t neighbor = static_cast<std::size_t>(ny * width + nx);
        values[index] = std::min(values[index], values[neighbor] + weight);
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            update(x, y, x - 1, y, 1.0f, distance);
            update(x, y, x, y - 1, 1.0f, distance);
            update(x, y, x - 1, y - 1, diagonal, distance);
            update(x, y, x + 1, y - 1, diagonal, distance);
        }
    }
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            update(x, y, x + 1, y, 1.0f, distance);
            update(x, y, x, y + 1, 1.0f, distance);
            update(x, y, x + 1, y + 1, diagonal, distance);
            update(x, y, x - 1, y + 1, diagonal, distance);
        }
    }
    return distance;
}

void append_boundary_distances(
    const std::vector<unsigned char>& boundary,
    const std::vector<float>& distance_to_other,
    std::vector<double>& output) {

    const std::size_t count = std::min(boundary.size(), distance_to_other.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (boundary[i] != 0 && distance_to_other[i] < 1.0e8f) {
            output.push_back(static_cast<double>(distance_to_other[i]));
        }
    }
}

}

ImageMetrics compare_rgba(
    std::span<const float> reference,
    std::span<const float> candidate,
    std::span<const float> reference_depth,
    std::span<const float> candidate_depth) {

    ImageMetrics metrics;
    const std::size_t count = std::min(reference.size(), candidate.size());
    if (count < 4) return metrics;

    const bool have_depth = reference_depth.size() >= count / 4 && candidate_depth.size() >= count / 4;

    double abs_sum = 0.0;
    double sq_sum = 0.0;
    double ref_sq_sum = 0.0;
    double foreground_sq_sum = 0.0;
    std::size_t channels = 0;
    std::size_t foreground_channels = 0;
    double peak = 1.0;
    std::vector<double> absolute_errors;
    absolute_errors.reserve((count / 4) * 3);

    for (std::size_t i = 0, pixel = 0; i + 3 < count; i += 4, ++pixel) {
        const bool foreground = !have_depth || reference_depth[pixel] < 1.0f || candidate_depth[pixel] < 1.0f;

        for (std::size_t c = 0; c < 3; ++c) {
            const double r = static_cast<double>(reference[i + c]);
            const double d = static_cast<double>(candidate[i + c]) - r;
            const double a = std::abs(d);
            abs_sum += a;
            sq_sum += d * d;
            ref_sq_sum += r * r;
            metrics.max_abs_error = std::max(metrics.max_abs_error, a);
            peak = std::max(peak, std::abs(r));
            absolute_errors.push_back(a);
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
    metrics.p95_abs_error = percentile(std::move(absolute_errors), 0.95);
    return metrics;
}

DepthMetrics compare_depth(std::span<const float> reference, std::span<const float> candidate) {
    DepthMetrics metrics;
    const std::size_t count = std::min(reference.size(), candidate.size());
    if (count == 0) return metrics;

    double abs_sum = 0.0;
    double sq_sum = 0.0;
    std::size_t measured = 0;
    std::vector<double> absolute_errors;
    absolute_errors.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (reference[i] >= 1.0f && candidate[i] >= 1.0f) continue;
        const double d = static_cast<double>(candidate[i]) - static_cast<double>(reference[i]);
        const double a = std::abs(d);
        abs_sum += a;
        sq_sum += d * d;
        metrics.max_abs_error = std::max(metrics.max_abs_error, a);
        absolute_errors.push_back(a);
        ++measured;
    }
    if (measured == 0) return metrics;
    metrics.mae = abs_sum / static_cast<double>(measured);
    metrics.rmse = std::sqrt(sq_sum / static_cast<double>(measured));
    metrics.p95_abs_error = percentile(std::move(absolute_errors), 0.95);
    return metrics;
}

SilhouetteMetrics compare_silhouette(
    std::span<const float> reference_depth,
    std::span<const float> candidate_depth,
    int width,
    int height) {

    SilhouetteMetrics metrics;
    if (width <= 0 || height <= 0) return metrics;
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (reference_depth.size() < count || candidate_depth.size() < count) return metrics;

    const auto reference = build_foreground_mask(reference_depth, count);
    const auto candidate = build_foreground_mask(candidate_depth, count);
    std::size_t mismatch = 0;
    for (std::size_t i = 0; i < count; ++i) mismatch += reference[i] != candidate[i] ? 1u : 0u;
    metrics.coverage_mismatch_fraction = static_cast<double>(mismatch) / static_cast<double>(count);

    const auto reference_boundary = build_boundary_mask(reference, width, height);
    const auto candidate_boundary = build_boundary_mask(candidate, width, height);
    const auto to_reference = chamfer_distance(reference_boundary, width, height);
    const auto to_candidate = chamfer_distance(candidate_boundary, width, height);

    std::vector<double> distances;
    distances.reserve(count / 16);
    append_boundary_distances(reference_boundary, to_candidate, distances);
    append_boundary_distances(candidate_boundary, to_reference, distances);
    if (distances.empty()) return metrics;

    double sum = 0.0;
    for (const double value : distances) {
        sum += value;
        metrics.boundary_max_distance_px = std::max(metrics.boundary_max_distance_px, value);
    }
    metrics.boundary_mean_distance_px = sum / static_cast<double>(distances.size());
    metrics.boundary_p95_distance_px = percentile(std::move(distances), 0.95);
    return metrics;
}

EqualityMetrics compare_exact(
    std::span<const float> reference_rgba,
    std::span<const float> candidate_rgba,
    std::span<const float> reference_depth,
    std::span<const float> candidate_depth) {

    EqualityMetrics result;
    result.rgba_exact = reference_rgba.size() == candidate_rgba.size()
        && (reference_rgba.empty()
            || std::memcmp(reference_rgba.data(), candidate_rgba.data(), reference_rgba.size_bytes()) == 0);
    result.depth_exact = reference_depth.size() == candidate_depth.size()
        && (reference_depth.empty()
            || std::memcmp(reference_depth.data(), candidate_depth.data(), reference_depth.size_bytes()) == 0);
    return result;
}

}
