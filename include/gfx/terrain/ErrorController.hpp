#pragma once

#include <gfx/terrain/ExperimentConfig.hpp>
#include <gfx/terrain/PatchSet.hpp>

#include <glm/vec3.hpp>

namespace gfx::terrain {

struct SelectionContext {
    glm::vec3 camera_position{};
    int viewport_height = 1;
    float vertical_fov_degrees = 60.0f;
    float error_budget_px = 1.0f;
    float feature_weight = 1.0f;
    float hysteresis = 0.15f;
    int distance_lod_bias = 0;
};

class ErrorController {
public:
    [[nodiscard]] int select_lod(
        ResearchMethod method,
        SurfacePatch& patch,
        int lod_count,
        const SelectionContext& context,
        bool force_finest = false) const;

    [[nodiscard]] double projected_error_px(
        const SurfacePatch& patch,
        int lod,
        const SelectionContext& context,
        bool feature_aware) const;

private:
    [[nodiscard]] int select_distance_lod(
        const SurfacePatch& patch,
        int lod_count,
        const SelectionContext& context) const;

    [[nodiscard]] int select_error_bounded_lod(
        SurfacePatch& patch,
        int lod_count,
        const SelectionContext& context,
        bool feature_aware) const;
};

}
