#include <gfx/terrain/Heightfield.hpp>

#include <algorithm>
#include <cmath>

namespace gfx::terrain {

float Heightfield::height(float x, float z) const {
    switch (scene_) {
    case SurfaceScene::SmoothHills: return smooth_hills(x, z);
    case SurfaceScene::SharpRidge: return sharp_ridge(x, z);
    case SurfaceScene::MixedFrequency: return mixed_frequency(x, z);
    case SurfaceScene::CliffBand: return cliff_band(x, z);
    }
    return 0.0f;
}

glm::vec3 Heightfield::normal(float x, float z) const {
    constexpr float h = 0.0025f;
    const float dx = (height(x + h, z) - height(x - h, z)) / (2.0f * h);
    const float dz = (height(x, z + h) - height(x, z - h)) / (2.0f * h);
    glm::vec3 n{-dx, 1.0f, -dz};
    const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (length > 0.0f) n /= length;
    return n;
}

float Heightfield::feature_strength(float x, float z) const {
    constexpr float h = 0.02f;
    const float c = height(x, z);
    const float dxx = std::abs(height(x + h, z) - 2.0f * c + height(x - h, z)) / (h * h);
    const float dzz = std::abs(height(x, z + h) - 2.0f * c + height(x, z - h)) / (h * h);
    const float dxz = std::abs(
        height(x + h, z + h) - height(x + h, z - h) -
        height(x - h, z + h) + height(x - h, z - h)) / (4.0f * h * h);
    return std::clamp((dxx + dzz + 0.5f * dxz) * 0.025f, 0.0f, 1.0f);
}

float Heightfield::smooth_hills(float x, float z) const {
    return 0.65f * std::sin(0.55f * x) * std::cos(0.48f * z) +
           0.18f * std::sin(1.25f * x + 0.4f * z);
}

float Heightfield::sharp_ridge(float x, float z) const {
    const float ridge = 0.9f * std::exp(-7.5f * std::abs(z - 0.25f * std::sin(0.6f * x)));
    return 0.25f * std::sin(0.45f * x) + ridge;
}

float Heightfield::mixed_frequency(float x, float z) const {
    const float low = 0.55f * std::sin(0.38f * x) * std::cos(0.44f * z);
    const float medium = 0.20f * std::sin(1.7f * x + 0.2f) * std::sin(1.35f * z);
    const float high = 0.055f * std::sin(6.5f * x + 1.7f * z);
    const float rock = 0.7f * std::exp(-0.32f * ((x - 2.1f) * (x - 2.1f) + (z + 1.4f) * (z + 1.4f)));
    return low + medium + high + rock;
}

float Heightfield::cliff_band(float x, float z) const {
    const float band = 0.85f * std::tanh(8.0f * (z + 0.2f * std::sin(0.7f * x)));
    return 0.22f * std::sin(0.55f * x) + 0.35f * band;
}

}
