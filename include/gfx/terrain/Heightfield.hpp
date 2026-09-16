#pragma once

#include <gfx/terrain/ExperimentConfig.hpp>

#include <glm/vec3.hpp>

#include <vector>

namespace gfx::terrain {

class Heightfield {
public:
    explicit Heightfield(SurfaceScene scene = SurfaceScene::MixedFrequency, float world_size = 16.0f);

    void set_scene(SurfaceScene scene, float world_size);
    [[nodiscard]] SurfaceScene scene() const { return scene_; }
    [[nodiscard]] float world_size() const { return world_size_; }

    [[nodiscard]] float height(float x, float z) const;
    [[nodiscard]] glm::vec3 normal(float x, float z) const;
    [[nodiscard]] float feature_strength(float x, float z) const;

    [[nodiscard]] bool has_grid() const { return !grid_.empty(); }
    [[nodiscard]] int grid_size() const { return grid_size_; }
    [[nodiscard]] const std::vector<float>& grid_data() const { return grid_; }
    [[nodiscard]] bool has_validity_mask() const { return !validity_mask_.empty(); }
    [[nodiscard]] const std::vector<unsigned char>& validity_data() const { return validity_mask_; }
    [[nodiscard]] float min_height() const { return min_height_; }
    [[nodiscard]] float max_height() const { return max_height_; }

private:
    [[nodiscard]] float smooth_hills(float x, float z) const;
    [[nodiscard]] float sharp_ridge(float x, float z) const;
    [[nodiscard]] float mixed_frequency(float x, float z) const;
    [[nodiscard]] float cliff_band(float x, float z) const;
    [[nodiscard]] float sample_grid(float x, float z) const;

    void generate_grid();
    void generate_eroded_mountain();
    void generate_plateau_canyon();
    void generate_alpine_escarpment();
    void generate_glacial_valley();
    void generate_river_basin();
    void generate_badlands();
    void generate_fractured_highlands();
    void load_mount_st_helens_dem();
    void thermal_erode(int iterations, float talus, float transport);
    void drainage_erode(int passes, float strength);
    void normalize_grid(float minimum, float maximum);
    void update_height_range();

    SurfaceScene scene_;
    float world_size_ = 16.0f;
    int grid_size_ = 513;
    std::vector<float> grid_;
    std::vector<unsigned char> validity_mask_;
    float min_height_ = -1.0f;
    float max_height_ = 1.0f;
};

}
