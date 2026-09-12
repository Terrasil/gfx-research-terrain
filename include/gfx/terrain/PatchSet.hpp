#pragma once

#include <gfx/terrain/ExperimentConfig.hpp>
#include <gfx/terrain/Heightfield.hpp>

#include <gfx/research/mesh.hpp>

#include <array>
#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

namespace gfx::terrain {

inline constexpr int max_lod_count = 6;

struct LodError {
    double max_height_error = 0.0;
    double rms_height_error = 0.0;
    double max_normal_error_degrees = 0.0;
    double max_feature_strength = 0.0;
    double rms_feature_strength = 0.0;
    std::size_t triangle_count = 0;
};

struct SurfacePatch {
    glm::vec3 center{};
    float bounding_radius = 0.0f;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
    int grid_x = 0;
    int grid_z = 0;
    std::array<gfx::research::Mesh, max_lod_count> meshes;
    std::array<LodError, max_lod_count> errors;
    int previous_lod = 0;
};

class PatchSet {
public:
    bool build(const ExperimentConfig& config, const Heightfield& heightfield);
    void reset_history(int lod);

    [[nodiscard]] int patches_per_side() const { return patches_per_side_; }
    [[nodiscard]] std::vector<SurfacePatch>& patches() { return patches_; }
    [[nodiscard]] const std::vector<SurfacePatch>& patches() const { return patches_; }

    [[nodiscard]] static int cells_for_lod(int max_cells_per_patch, int lod);

private:
    gfx::research::Mesh build_patch_mesh(
        const SurfacePatch& patch,
        int cells,
        const Heightfield& heightfield) const;

    LodError measure_lod_error(
        const SurfacePatch& patch,
        int cells,
        int samples_per_side,
        const Heightfield& heightfield) const;

    [[nodiscard]] float approximate_height(
        const SurfacePatch& patch,
        int cells,
        float x,
        float z,
        const Heightfield& heightfield) const;

    std::vector<SurfacePatch> patches_;
    int patches_per_side_ = 0;
};

}
