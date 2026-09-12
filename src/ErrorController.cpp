#include <gfx/terrain/ErrorController.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gfx::terrain {

int ErrorController::select_lod(
    ResearchMethod method,
    SurfacePatch& patch,
    int lod_count,
    const SelectionContext& context,
    bool force_finest) const {

    const int finest = lod_count - 1;
    if (force_finest || method == ResearchMethod::Reference) {
        patch.previous_lod = finest;
        return finest;
    }

    if (method == ResearchMethod::DistanceLod) {
        const int selected = select_distance_lod(patch, lod_count, context);
        patch.previous_lod = selected;
        return selected;
    }

    return select_error_bounded_lod(
        patch,
        lod_count,
        context,
        method == ResearchMethod::ErrorBoundedFeatureAware);
}

double ErrorController::projected_error_px(
    const SurfacePatch& patch,
    int lod,
    const SelectionContext& context,
    bool feature_aware) const {

    const auto& error = patch.errors[static_cast<std::size_t>(lod)];
    const glm::vec3 delta = patch.center - context.camera_position;
    const double center_distance = std::sqrt(static_cast<double>(
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z));
    const double nearest_distance = std::max(
        0.05,
        center_distance - static_cast<double>(patch.bounding_radius));

    const double fov_radians = static_cast<double>(context.vertical_fov_degrees) * std::numbers::pi / 180.0;
    const double focal_pixels = static_cast<double>(context.viewport_height) /
        (2.0 * std::tan(0.5 * fov_radians));

    const double geometric_px = error.max_height_error * focal_pixels / nearest_distance;
    if (!feature_aware) return geometric_px;

    const double normal_radians = error.max_normal_error_degrees * std::numbers::pi / 180.0;
    const double normal_proxy_px = static_cast<double>(patch.bounding_radius) *
        std::sin(std::clamp(normal_radians, 0.0, std::numbers::pi / 2.0)) *
        focal_pixels / nearest_distance;
    const double feature = std::clamp(error.max_feature_strength, 0.0, 1.0);
    const double weight = std::max(0.0, static_cast<double>(context.feature_weight));

    return geometric_px + weight * (0.25 * feature * geometric_px + 0.05 * normal_proxy_px);
}

int ErrorController::select_distance_lod(
    const SurfacePatch& patch,
    int lod_count,
    const SelectionContext& context) const {

    const glm::vec3 delta = patch.center - context.camera_position;
    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    const float normalized = std::max(0.0f, distance / 3.5f);
    const int reduction = static_cast<int>(std::floor(std::log2(1.0f + normalized))) + context.distance_lod_bias;
    return std::clamp(lod_count - 1 - reduction, 0, lod_count - 1);
}

int ErrorController::select_error_bounded_lod(
    SurfacePatch& patch,
    int lod_count,
    const SelectionContext& context,
    bool feature_aware) const {

    int selected = lod_count - 1;
    for (int lod = 0; lod < lod_count; ++lod) {
        if (projected_error_px(patch, lod, context, feature_aware) <= context.error_budget_px) {
            selected = lod;
            break;
        }
    }

    const int previous = std::clamp(patch.previous_lod, 0, lod_count - 1);
    if (context.hysteresis > 0.0f) {
        if (selected < previous) {
            const double candidate_error = projected_error_px(patch, selected, context, feature_aware);
            const double lower_threshold = context.error_budget_px *
                (1.0 - static_cast<double>(context.hysteresis));
            if (candidate_error > lower_threshold) selected = previous;
        } else if (selected > previous) {
            const double previous_error = projected_error_px(patch, previous, context, feature_aware);
            const double upper_threshold = context.error_budget_px *
                (1.0 + static_cast<double>(context.hysteresis));
            if (previous_error <= upper_threshold) selected = previous;
        }
    }

    patch.previous_lod = selected;
    return selected;
}

}
