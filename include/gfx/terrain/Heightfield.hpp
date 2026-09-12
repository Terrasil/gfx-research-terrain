#pragma once

#include <gfx/terrain/ExperimentConfig.hpp>

#include <glm/vec3.hpp>

namespace gfx::terrain {

class Heightfield {
public:
    explicit Heightfield(SurfaceScene scene = SurfaceScene::MixedFrequency) : scene_(scene) {}

    void set_scene(SurfaceScene scene) { scene_ = scene; }
    [[nodiscard]] SurfaceScene scene() const { return scene_; }

    [[nodiscard]] float height(float x, float z) const;
    [[nodiscard]] glm::vec3 normal(float x, float z) const;
    [[nodiscard]] float feature_strength(float x, float z) const;

private:
    [[nodiscard]] float smooth_hills(float x, float z) const;
    [[nodiscard]] float sharp_ridge(float x, float z) const;
    [[nodiscard]] float mixed_frequency(float x, float z) const;
    [[nodiscard]] float cliff_band(float x, float z) const;

    SurfaceScene scene_;
};

}
