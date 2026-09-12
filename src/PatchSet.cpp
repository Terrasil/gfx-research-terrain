#include <gfx/terrain/PatchSet.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include <glm/geometric.hpp>

namespace gfx::terrain {

int PatchSet::cells_for_lod(int max_cells_per_patch, int lod) {
    if (max_cells_per_patch <= 1) return 1;
    return std::min(max_cells_per_patch, 2 << std::max(lod, 0));
}

bool PatchSet::build(const ExperimentConfig& config, const Heightfield& heightfield) {
    if (config.patches_per_side <= 0 || config.lod_count <= 0 ||
        config.lod_count > max_lod_count || config.error_samples_per_side < 3) {
        return false;
    }

    patches_.clear();
    patches_per_side_ = config.patches_per_side;
    patches_.reserve(static_cast<std::size_t>(config.patches_per_side * config.patches_per_side));

    const float half_world = config.world_size * 0.5f;
    const float patch_size = config.world_size / static_cast<float>(config.patches_per_side);

    for (int pz = 0; pz < config.patches_per_side; ++pz) {
        for (int px = 0; px < config.patches_per_side; ++px) {
            SurfacePatch patch;
            patch.grid_x = px;
            patch.grid_z = pz;
            patch.min_x = -half_world + static_cast<float>(px) * patch_size;
            patch.max_x = patch.min_x + patch_size;
            patch.min_z = -half_world + static_cast<float>(pz) * patch_size;
            patch.max_z = patch.min_z + patch_size;

            float min_y = std::numeric_limits<float>::max();
            float max_y = std::numeric_limits<float>::lowest();
            constexpr int bound_samples = 17;
            for (int z = 0; z < bound_samples; ++z) {
                const float tz = static_cast<float>(z) / static_cast<float>(bound_samples - 1);
                const float wz = patch.min_z + (patch.max_z - patch.min_z) * tz;
                for (int x = 0; x < bound_samples; ++x) {
                    const float tx = static_cast<float>(x) / static_cast<float>(bound_samples - 1);
                    const float wx = patch.min_x + (patch.max_x - patch.min_x) * tx;
                    const float y = heightfield.height(wx, wz);
                    min_y = std::min(min_y, y);
                    max_y = std::max(max_y, y);
                }
            }
            patch.min_y = min_y;
            patch.max_y = max_y;
            patch.center = {
                0.5f * (patch.min_x + patch.max_x),
                0.5f * (min_y + max_y),
                0.5f * (patch.min_z + patch.max_z)
            };
            const glm::vec3 half_extent{
                0.5f * (patch.max_x - patch.min_x),
                0.5f * (max_y - min_y),
                0.5f * (patch.max_z - patch.min_z)
            };
            patch.bounding_radius = glm::length(half_extent);

            for (int lod = 0; lod < config.lod_count; ++lod) {
                const int cells = cells_for_lod(config.max_cells_per_patch, lod);
                patch.meshes[static_cast<std::size_t>(lod)] = build_patch_mesh(patch, cells, heightfield);
                patch.errors[static_cast<std::size_t>(lod)] = measure_lod_error(
                    patch, cells, config.error_samples_per_side, heightfield);
            }
            patch.previous_lod = config.lod_count - 1;
            patches_.push_back(std::move(patch));
        }
    }
    return true;
}

void PatchSet::reset_history(int lod) {
    for (auto& patch : patches_) patch.previous_lod = lod;
}

gfx::research::Mesh PatchSet::build_patch_mesh(
    const SurfacePatch& patch,
    int cells,
    const Heightfield& heightfield) const {

    const int vertices_per_side = cells + 1;
    const float step_x = (patch.max_x - patch.min_x) / static_cast<float>(cells);
    const float step_z = (patch.max_z - patch.min_z) / static_cast<float>(cells);

    std::vector<gfx::research::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(static_cast<std::size_t>(vertices_per_side * vertices_per_side));
    indices.reserve(static_cast<std::size_t>(cells * cells * 6));

    for (int z = 0; z <= cells; ++z) {
        const float tz = static_cast<float>(z) / static_cast<float>(cells);
        const float wz = patch.min_z + (patch.max_z - patch.min_z) * tz;
        for (int x = 0; x <= cells; ++x) {
            const float tx = static_cast<float>(x) / static_cast<float>(cells);
            const float wx = patch.min_x + (patch.max_x - patch.min_x) * tx;

            const float x0 = std::max(patch.min_x, wx - step_x);
            const float x1 = std::min(patch.max_x, wx + step_x);
            const float z0 = std::max(patch.min_z, wz - step_z);
            const float z1 = std::min(patch.max_z, wz + step_z);
            const float dx = (heightfield.height(x1, wz) - heightfield.height(x0, wz)) /
                std::max(x1 - x0, 1.0e-6f);
            const float dz = (heightfield.height(wx, z1) - heightfield.height(wx, z0)) /
                std::max(z1 - z0, 1.0e-6f);

            gfx::research::Vertex vertex;
            vertex.position = {wx, heightfield.height(wx, wz), wz};
            vertex.normal = glm::normalize(glm::vec3{-dx, 1.0f, -dz});
            vertex.uv = {tx, tz};
            vertices.push_back(vertex);
        }
    }

    for (int z = 0; z < cells; ++z) {
        for (int x = 0; x < cells; ++x) {
            const std::uint32_t a = static_cast<std::uint32_t>(z * vertices_per_side + x);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + static_cast<std::uint32_t>(vertices_per_side);
            const std::uint32_t d = c + 1;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }

    return gfx::research::Mesh(vertices, indices);
}

LodError PatchSet::measure_lod_error(
    const SurfacePatch& patch,
    int cells,
    int samples_per_side,
    const Heightfield& heightfield) const {

    double sum_sq = 0.0;
    double max_error = 0.0;
    double max_normal_angle = 0.0;
    double feature_sq_sum = 0.0;
    double max_feature = 0.0;
    std::size_t sample_count = 0;

    const float derivative_step = 0.25f * std::min(
        (patch.max_x - patch.min_x) / static_cast<float>(cells),
        (patch.max_z - patch.min_z) / static_cast<float>(cells));

    for (int sz = 0; sz < samples_per_side; ++sz) {
        const float tz = static_cast<float>(sz) / static_cast<float>(samples_per_side - 1);
        const float z = patch.min_z + (patch.max_z - patch.min_z) * tz;
        for (int sx = 0; sx < samples_per_side; ++sx) {
            const float tx = static_cast<float>(sx) / static_cast<float>(samples_per_side - 1);
            const float x = patch.min_x + (patch.max_x - patch.min_x) * tx;

            const float truth = heightfield.height(x, z);
            const float approx = approximate_height(patch, cells, x, z, heightfield);
            const double e = std::abs(static_cast<double>(truth - approx));
            max_error = std::max(max_error, e);
            sum_sq += e * e;

            const glm::vec3 n_truth = heightfield.normal(x, z);
            const float x0 = std::max(patch.min_x, x - derivative_step);
            const float x1 = std::min(patch.max_x, x + derivative_step);
            const float z0 = std::max(patch.min_z, z - derivative_step);
            const float z1 = std::min(patch.max_z, z + derivative_step);
            const float dx = (approximate_height(patch, cells, x1, z, heightfield) -
                              approximate_height(patch, cells, x0, z, heightfield)) /
                std::max(x1 - x0, 1.0e-6f);
            const float dz = (approximate_height(patch, cells, x, z1, heightfield) -
                              approximate_height(patch, cells, x, z0, heightfield)) /
                std::max(z1 - z0, 1.0e-6f);
            const glm::vec3 n_approx = glm::normalize(glm::vec3{-dx, 1.0f, -dz});
            const float dot_value = std::clamp(glm::dot(n_truth, n_approx), -1.0f, 1.0f);
            const double angle = std::acos(static_cast<double>(dot_value)) * 180.0 / std::numbers::pi;
            max_normal_angle = std::max(max_normal_angle, angle);

            const double feature = static_cast<double>(heightfield.feature_strength(x, z));
            max_feature = std::max(max_feature, feature);
            feature_sq_sum += feature * feature;
            ++sample_count;
        }
    }

    LodError result;
    result.max_height_error = max_error;
    result.rms_height_error = sample_count > 0 ? std::sqrt(sum_sq / static_cast<double>(sample_count)) : 0.0;
    result.max_normal_error_degrees = max_normal_angle;
    result.max_feature_strength = max_feature;
    result.rms_feature_strength = sample_count > 0
        ? std::sqrt(feature_sq_sum / static_cast<double>(sample_count))
        : 0.0;
    result.triangle_count = static_cast<std::size_t>(cells * cells * 2);
    return result;
}

float PatchSet::approximate_height(
    const SurfacePatch& patch,
    int cells,
    float x,
    float z,
    const Heightfield& heightfield) const {

    x = std::clamp(x, patch.min_x, patch.max_x);
    z = std::clamp(z, patch.min_z, patch.max_z);
    const float ux = (x - patch.min_x) / (patch.max_x - patch.min_x) * static_cast<float>(cells);
    const float uz = (z - patch.min_z) / (patch.max_z - patch.min_z) * static_cast<float>(cells);
    const int ix = std::min(static_cast<int>(std::floor(ux)), cells - 1);
    const int iz = std::min(static_cast<int>(std::floor(uz)), cells - 1);
    const float fx = ux - static_cast<float>(ix);
    const float fz = uz - static_cast<float>(iz);

    const float step_x = (patch.max_x - patch.min_x) / static_cast<float>(cells);
    const float step_z = (patch.max_z - patch.min_z) / static_cast<float>(cells);
    const float x0 = patch.min_x + static_cast<float>(ix) * step_x;
    const float x1 = x0 + step_x;
    const float z0 = patch.min_z + static_cast<float>(iz) * step_z;
    const float z1 = z0 + step_z;

    const float h00 = heightfield.height(x0, z0);
    const float h10 = heightfield.height(x1, z0);
    const float h01 = heightfield.height(x0, z1);
    const float h11 = heightfield.height(x1, z1);

    if (fx + fz <= 1.0f) {
        return h00 + fx * (h10 - h00) + fz * (h01 - h00);
    }
    return h11 + (1.0f - fx) * (h01 - h11) + (1.0f - fz) * (h10 - h11);
}

}
