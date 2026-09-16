#include <gfx/terrain/Heightfield.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef GFX_TERRAIN_SOURCE_DIR
#define GFX_TERRAIN_SOURCE_DIR "."
#endif

namespace gfx::terrain {

namespace {

float hash01(int x, int y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<std::uint32_t>(y) * 0xd8163841u;
    h ^= seed * 0xcb1ab31fu;
    h ^= h >> 13u;
    h *= 0x85ebca6bu;
    h ^= h >> 16u;
    return static_cast<float>(h & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

float smoothstep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float smootherstep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float value_noise(float x, float y, std::uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float sx = smoothstep01(fx);
    const float sy = smoothstep01(fy);
    const float a = hash01(ix, iy, seed);
    const float b = hash01(ix + 1, iy, seed);
    const float c = hash01(ix, iy + 1, seed);
    const float d = hash01(ix + 1, iy + 1, seed);
    return std::lerp(std::lerp(a, b, sx), std::lerp(c, d, sx), sy) * 2.0f - 1.0f;
}

float fbm(float x, float y, std::uint32_t seed, int octaves = 6) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * value_noise(x * frequency, y * frequency, seed + static_cast<std::uint32_t>(i) * 97u);
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.03f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float ridged(float x, float y, std::uint32_t seed, int octaves = 6) {
    float sum = 0.0f;
    float amplitude = 0.55f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        const std::uint32_t octave_seed = seed + static_cast<std::uint32_t>(i) * 131u;
        const float n = 1.0f - std::abs(value_noise(x * frequency, y * frequency, octave_seed));
        sum += amplitude * n * n;
        norm += amplitude;
        amplitude *= 0.52f;
        frequency *= 2.07f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float billow(float x, float y, std::uint32_t seed, int octaves = 5) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        const std::uint32_t octave_seed = seed + static_cast<std::uint32_t>(i) * 173u;
        const float n = std::abs(value_noise(x * frequency, y * frequency, octave_seed));
        sum += amplitude * (n * 2.0f - 1.0f);
        norm += amplitude;
        amplitude *= 0.52f;
        frequency *= 2.11f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float gaussian(float x, float z, float cx, float cz, float scale) {
    const float dx = x - cx;
    const float dz = z - cz;
    return std::exp(-scale * (dx * dx + dz * dz));
}

float edge_fade(float u, float v) {
    const float fx = smootherstep01(u / 0.018f) * smootherstep01((1.0f - u) / 0.018f);
    const float fz = smootherstep01(v / 0.018f) * smootherstep01((1.0f - v) / 0.018f);
    return std::min(fx, fz);
}

} // namespace

Heightfield::Heightfield(SurfaceScene scene, float world_size)
    : scene_(scene), world_size_(world_size) {
    generate_grid();
}

void Heightfield::set_scene(SurfaceScene scene, float world_size) {
    scene_ = scene;
    world_size_ = world_size;
    generate_grid();
}

float Heightfield::height(float x, float z) const {
    switch (scene_) {
    case SurfaceScene::SmoothHills: return smooth_hills(x, z);
    case SurfaceScene::SharpRidge: return sharp_ridge(x, z);
    case SurfaceScene::MixedFrequency: return mixed_frequency(x, z);
    case SurfaceScene::CliffBand: return cliff_band(x, z);
    case SurfaceScene::ErodedMountain:
    case SurfaceScene::PlateauCanyon:
    case SurfaceScene::AlpineEscarpment:
    case SurfaceScene::GlacialValley:
    case SurfaceScene::RiverBasin:
    case SurfaceScene::Badlands:
    case SurfaceScene::FracturedHighlands:
    case SurfaceScene::MountStHelensDem:
        return sample_grid(x, z);
    }
    return 0.0f;
}

glm::vec3 Heightfield::normal(float x, float z) const {
    const float h = std::max(world_size_ / static_cast<float>(std::max(grid_size_ - 1, 1)) * 0.75f, 0.0025f);
    const float dx = (height(x + h, z) - height(x - h, z)) / (2.0f * h);
    const float dz = (height(x, z + h) - height(x, z - h)) / (2.0f * h);
    glm::vec3 n{-dx, 1.0f, -dz};
    const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (length > 0.0f) n /= length;
    return n;
}

float Heightfield::feature_strength(float x, float z) const {
    const float h = std::max(world_size_ / static_cast<float>(std::max(grid_size_ - 1, 1)) * 1.5f, 0.02f);
    const float c = height(x, z);
    const float dxx = std::abs(height(x + h, z) - 2.0f * c + height(x - h, z)) / (h * h);
    const float dzz = std::abs(height(x, z + h) - 2.0f * c + height(x, z - h)) / (h * h);
    const float dxz = std::abs(
        height(x + h, z + h) - height(x + h, z - h) -
        height(x - h, z + h) + height(x - h, z - h)) / (4.0f * h * h);
    return std::clamp((dxx + dzz + 0.5f * dxz) * 0.018f, 0.0f, 1.0f);
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

float Heightfield::sample_grid(float x, float z) const {
    if (grid_.empty()) return 0.0f;
    const float half = world_size_ * 0.5f;
    const float u = std::clamp((x + half) / world_size_, 0.0f, 1.0f) * static_cast<float>(grid_size_ - 1);
    const float v = std::clamp((z + half) / world_size_, 0.0f, 1.0f) * static_cast<float>(grid_size_ - 1);
    const int x0 = std::min(static_cast<int>(std::floor(u)), grid_size_ - 2);
    const int z0 = std::min(static_cast<int>(std::floor(v)), grid_size_ - 2);
    const int x1 = x0 + 1;
    const int z1 = z0 + 1;
    const float fx = u - static_cast<float>(x0);
    const float fz = v - static_cast<float>(z0);
    const auto at = [&](int gx, int gz) {
        return grid_[static_cast<std::size_t>(gz * grid_size_ + gx)];
    };
    return std::lerp(std::lerp(at(x0, z0), at(x1, z0), fx), std::lerp(at(x0, z1), at(x1, z1), fx), fz);
}

void Heightfield::generate_grid() {
    grid_.clear();
    validity_mask_.clear();
    if (scene_ != SurfaceScene::MountStHelensDem) grid_size_ = 513;
    switch (scene_) {
    case SurfaceScene::ErodedMountain: generate_eroded_mountain(); break;
    case SurfaceScene::PlateauCanyon: generate_plateau_canyon(); break;
    case SurfaceScene::AlpineEscarpment: generate_alpine_escarpment(); break;
    case SurfaceScene::GlacialValley: generate_glacial_valley(); break;
    case SurfaceScene::RiverBasin: generate_river_basin(); break;
    case SurfaceScene::Badlands: generate_badlands(); break;
    case SurfaceScene::FracturedHighlands: generate_fractured_highlands(); break;
    case SurfaceScene::MountStHelensDem: load_mount_st_helens_dem(); break;
    default: break;
    }
    update_height_range();
}

void Heightfield::generate_eroded_mountain() {
    grid_.resize(static_cast<std::size_t>(grid_size_ * grid_size_));
    for (int z = 0; z < grid_size_; ++z) {
        for (int x = 0; x < grid_size_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(grid_size_ - 1);
            const float v = static_cast<float>(z) / static_cast<float>(grid_size_ - 1);
            const float px = (u - 0.5f) * 4.2f;
            const float pz = (v - 0.5f) * 4.2f;
            const float warp_x = px + 0.43f * fbm(px * 0.72f + 9.0f, pz * 0.72f, 1701u, 5);
            const float warp_z = pz + 0.43f * fbm(px * 0.72f, pz * 0.72f - 7.0f, 2701u, 5);
            const float ridge = ridged(warp_x * 0.76f, warp_z * 0.76f, 42u, 7);
            const float shoulder = billow(warp_x * 0.48f, warp_z * 0.48f, 43u, 5);
            const float detail = fbm(warp_x * 2.15f, warp_z * 2.15f, 77u, 6);
            const float radial = std::exp(-0.16f * (px * px + pz * pz));
            float h = 3.25f * std::pow(std::max(ridge, 0.0f), 1.25f) * radial;
            h += 0.50f * shoulder * radial + 0.24f * detail + 0.18f * radial;
            h *= edge_fade(u, v);
            grid_[static_cast<std::size_t>(z * grid_size_ + x)] = h;
        }
    }
    drainage_erode(4, 0.095f);
    thermal_erode(18, 0.022f, 0.19f);
    normalize_grid(-0.35f, 3.55f);
}

void Heightfield::generate_plateau_canyon() {
    grid_.resize(static_cast<std::size_t>(grid_size_ * grid_size_));
    for (int z = 0; z < grid_size_; ++z) {
        for (int x = 0; x < grid_size_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(grid_size_ - 1);
            const float v = static_cast<float>(z) / static_cast<float>(grid_size_ - 1);
            const float px = (u - 0.5f) * 4.5f;
            const float pz = (v - 0.5f) * 4.5f;
            const float warp = 0.34f * fbm(px * 0.55f, pz * 0.55f, 980u, 5);
            const float n = fbm(px * 0.62f + warp, pz * 0.62f - warp, 990u, 6);
            const float plateau_mask = 0.5f + 0.5f * std::tanh(5.0f * (n + 0.08f));
            const float plateau = 1.75f * plateau_mask;
            const float river_center = 0.30f * std::sin(1.22f * px) + 0.13f * std::sin(3.9f * px + 0.7f);
            const float canyon_distance = std::abs(pz - river_center);
            const float canyon = std::exp(-7.0f * canyon_distance);
            const float tributary_a = std::exp(-10.5f * std::abs(px + 0.95f + 0.30f * std::sin(1.6f * pz)));
            const float tributary_b = std::exp(-11.0f * std::abs(px - 1.10f - 0.22f * std::sin(2.2f * pz + 1.2f)));
            const float terrace = 0.075f * std::sin(12.5f * plateau + 1.4f * n);
            const float micro = 0.15f * fbm(px * 2.4f + 2.0f, pz * 2.4f - 1.0f, 1990u, 6);
            float h = plateau + terrace + micro;
            h -= 1.35f * canyon * (0.55f + 0.45f * plateau_mask);
            h -= 0.55f * (tributary_a + tributary_b) * plateau_mask;
            h = std::lerp(-0.18f, h, edge_fade(u, v));
            grid_[static_cast<std::size_t>(z * grid_size_ + x)] = h;
        }
    }
    drainage_erode(3, 0.055f);
    thermal_erode(12, 0.032f, 0.13f);
    normalize_grid(-0.50f, 2.20f);
}

void Heightfield::generate_alpine_escarpment() {
    grid_.resize(static_cast<std::size_t>(grid_size_ * grid_size_));
    for (int z = 0; z < grid_size_; ++z) {
        for (int x = 0; x < grid_size_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(grid_size_ - 1);
            const float v = static_cast<float>(z) / static_cast<float>(grid_size_ - 1);
            const float px = (u - 0.5f) * 4.6f;
            const float pz = (v - 0.5f) * 4.6f;

            const float warp_a = fbm(px * 0.66f + 4.0f, pz * 0.66f - 3.0f, 771u, 5);
            const float warp_b = fbm(px * 1.15f - 5.0f, pz * 1.15f + 2.0f, 772u, 4);
            const float boundary = 0.24f * std::sin(px * 1.18f) + 0.11f * std::sin(px * 3.35f + 0.7f)
                + 0.19f * warp_a + 0.055f * warp_b;
            const float highland = 0.5f + 0.5f * std::tanh(8.2f * (pz - boundary));

            const float alpine_ridges = ridged(px * 0.70f + 0.20f * warp_a, pz * 0.70f, 4101u, 7);
            const float high_detail = fbm(px * 1.85f, pz * 1.85f, 4102u, 6);
            const float valley_rock = ridged(px * 1.0f, pz * 1.0f, 4103u, 6);
            const float valley_detail = fbm(px * 2.4f - 2.0f, pz * 2.4f + 5.0f, 4104u, 5);

            const float peak_a = gaussian(px, pz, 0.72f, 1.28f, 0.68f);
            const float peak_b = gaussian(px, pz, -0.92f, 1.48f, 0.88f);
            const float peak_c = gaussian(px, pz, 1.52f, 1.02f, 1.15f);
            const float peak_d = gaussian(px, pz, -1.62f, 0.88f, 1.35f);

            const float cliff_noise = fbm(px * 1.8f, pz * 1.8f, 4105u, 4);
            const float cliff_strata = 0.085f * std::sin((pz - boundary) * 43.0f + 2.4f * cliff_noise);
            const float cliff_mask = std::exp(-12.5f * std::abs(pz - boundary));
            const float gully_noise = ridged(px * 2.25f + 3.0f, pz * 2.25f - 2.0f, 4106u, 5);
            const float gullies = std::pow(std::clamp(gully_noise, 0.0f, 1.0f), 3.0f);

            float h = -0.48f;
            h += (0.42f * valley_rock + 0.17f * valley_detail) * (1.0f - highland);
            h += highland * (2.20f + 1.18f * alpine_ridges + 0.28f * high_detail);
            h += highland * (1.30f * peak_a + 1.02f * peak_b + 0.76f * peak_c + 0.58f * peak_d);
            h += cliff_mask * cliff_strata;
            h -= (0.19f + 0.27f * highland) * gullies;
            h = std::lerp(-0.43f, h, edge_fade(u, v));
            grid_[static_cast<std::size_t>(z * grid_size_ + x)] = h;
        }
    }
    drainage_erode(3, 0.060f);
    thermal_erode(7, 0.050f, 0.085f);
    normalize_grid(-0.55f, 4.05f);
}

void Heightfield::generate_glacial_valley() {
    grid_.resize(static_cast<std::size_t>(grid_size_ * grid_size_));
    for (int z = 0; z < grid_size_; ++z) {
        for (int x = 0; x < grid_size_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(grid_size_ - 1);
            const float v = static_cast<float>(z) / static_cast<float>(grid_size_ - 1);
            const float px = (u - 0.5f) * 4.8f;
            const float pz = (v - 0.5f) * 4.8f;
            const float meander = 0.22f * std::sin(pz * 0.95f) + 0.08f * std::sin(pz * 2.8f + 0.4f);
            const float cross = std::abs(px - meander);
            const float valley_floor = std::pow(std::clamp(cross / 2.35f, 0.0f, 1.0f), 1.75f);
            const float wall = 3.1f * valley_floor;
            const float mountain = ridged(px * 0.62f, pz * 0.60f, 5201u, 7);
            const float long_noise = fbm(px * 0.55f + 7.0f, pz * 0.42f, 5202u, 5);
            const float floor_noise = fbm(px * 2.5f, pz * 2.0f, 5203u, 5);
            const float side_mask = smoothstep01((cross - 0.32f) / 1.25f);
            const float cirque_a = gaussian(px, pz, -1.55f, 1.40f, 1.45f);
            const float cirque_b = gaussian(px, pz, 1.62f, 0.62f, 1.65f);
            const float moraine = 0.12f * std::exp(-7.0f * std::abs(cross - 0.52f)) *
                (0.4f + 0.6f * fbm(px * 2.1f, pz * 2.1f, 5204u, 4));
            float h = -0.42f + wall;
            h += side_mask * (1.05f * mountain + 0.28f * long_noise);
            h += (1.0f - side_mask) * 0.10f * floor_noise;
            h -= 0.72f * side_mask * (cirque_a + cirque_b);
            h += moraine;
            h += 0.10f * (pz / 2.4f);
            h = std::lerp(-0.38f, h, edge_fade(u, v));
            grid_[static_cast<std::size_t>(z * grid_size_ + x)] = h;
        }
    }
    drainage_erode(3, 0.045f);
    thermal_erode(10, 0.035f, 0.10f);
    normalize_grid(-0.55f, 3.75f);
}

void Heightfield::generate_river_basin() {
    grid_.resize(static_cast<std::size_t>(grid_size_ * grid_size_));
    for (int z = 0; z < grid_size_; ++z) {
        for (int x = 0; x < grid_size_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(grid_size_ - 1);
            const float v = static_cast<float>(z) / static_cast<float>(grid_size_ - 1);
            const float px = (u - 0.5f) * 5.0f;
            const float pz = (v - 0.5f) * 5.0f;
            const float warp_x = px + 0.36f * fbm(px * 0.55f, pz * 0.55f, 6101u, 5);
            const float warp_z = pz + 0.36f * fbm(px * 0.55f + 4.0f, pz * 0.55f, 6102u, 5);
            const float broad = 0.82f * fbm(warp_x * 0.55f, warp_z * 0.55f, 6103u, 7);
            const float hills = 0.52f * billow(warp_x * 0.82f, warp_z * 0.82f, 6104u, 6);
            const float main_center = 0.32f * std::sin(px * 0.92f) + 0.10f * std::sin(px * 2.9f + 0.8f);
            const float main = std::exp(-9.0f * std::abs(pz - main_center));
            const float branch_a_center = -0.78f + 0.48f * pz + 0.10f * std::sin(pz * 3.0f);
            const float branch_b_center = 1.12f - 0.52f * pz + 0.08f * std::sin(pz * 3.6f + 1.1f);
            const float branch_a = std::exp(-13.0f * std::abs(px - branch_a_center));
            const float branch_b = std::exp(-13.0f * std::abs(px - branch_b_center));
            const float channel = std::max(main, 0.68f * std::max(branch_a, branch_b));
            const float fine = 0.10f * fbm(px * 3.0f, pz * 3.0f, 6105u, 5);
            float h = 0.42f + broad + hills + fine;
            h -= 0.92f * channel * (0.78f + 0.22f * smoothstep01(h));
            h += 0.12f * (0.5f - v);
            h = std::lerp(-0.12f, h, edge_fade(u, v));
            grid_[static_cast<std::size_t>(z * grid_size_ + x)] = h;
        }
    }
    drainage_erode(6, 0.075f);
    thermal_erode(9, 0.028f, 0.11f);
    normalize_grid(-0.55f, 2.55f);
}

void Heightfield::generate_badlands() {
    grid_.resize(static_cast<std::size_t>(grid_size_ * grid_size_));
    for (int z = 0; z < grid_size_; ++z) {
        for (int x = 0; x < grid_size_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(grid_size_ - 1);
            const float v = static_cast<float>(z) / static_cast<float>(grid_size_ - 1);
            const float px = (u - 0.5f) * 5.2f;
            const float pz = (v - 0.5f) * 5.2f;
            const float warp = 0.42f * fbm(px * 0.48f, pz * 0.48f, 7101u, 5);
            const float base = 0.78f * fbm(px * 0.58f + warp, pz * 0.58f - warp, 7102u, 7);
            const float ribs = ridged(px * 1.55f + warp, pz * 1.55f, 7103u, 7);
            const float gullies = ridged(px * 3.1f, pz * 3.1f, 7104u, 5);
            const float mesa_mask = 0.5f + 0.5f * std::tanh(4.6f * (base + 0.05f));
            const float terracing = 0.085f * std::sin((1.35f * mesa_mask + 0.25f * base) * 28.0f);
            const float fine = 0.075f * fbm(px * 5.0f, pz * 5.0f, 7105u, 4);
            float h = 0.28f + 1.85f * mesa_mask + 0.48f * base;
            h += 0.42f * ribs * mesa_mask - 0.38f * gullies * mesa_mask + terracing + fine;
            h = std::lerp(-0.20f, h, edge_fade(u, v));
            grid_[static_cast<std::size_t>(z * grid_size_ + x)] = h;
        }
    }
    drainage_erode(7, 0.085f);
    thermal_erode(5, 0.050f, 0.065f);
    normalize_grid(-0.45f, 2.85f);
}

void Heightfield::generate_fractured_highlands() {
    grid_.resize(static_cast<std::size_t>(grid_size_ * grid_size_));
    for (int z = 0; z < grid_size_; ++z) {
        for (int x = 0; x < grid_size_; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(grid_size_ - 1);
            const float v = static_cast<float>(z) / static_cast<float>(grid_size_ - 1);
            const float px = (u - 0.5f) * 4.9f;
            const float pz = (v - 0.5f) * 4.9f;
            const float warp = fbm(px * 0.62f, pz * 0.62f, 8101u, 5);
            const float highland = 1.45f * ridged(px * 0.68f + 0.22f * warp, pz * 0.68f, 8102u, 7);
            const float basin = -0.62f * gaussian(px, pz, -0.45f, -0.62f, 0.58f);
            const float fault_a = 0.44f * std::tanh(8.0f * (0.74f * px + 0.38f * pz - 0.22f - 0.12f * warp));
            const float fault_b = 0.30f * std::tanh(9.0f * (-0.48f * px + 0.82f * pz + 0.55f));
            const float ridge_detail = 0.28f * ridged(px * 2.15f, pz * 2.15f, 8103u, 6);
            const float fine = 0.10f * fbm(px * 4.0f, pz * 4.0f, 8104u, 5);
            const float shelf = 0.14f * std::sin((highland + fault_a - fault_b) * 12.0f);
            float h = 0.35f + highland + basin + fault_a - fault_b + ridge_detail + fine + shelf;
            h = std::lerp(-0.25f, h, edge_fade(u, v));
            grid_[static_cast<std::size_t>(z * grid_size_ + x)] = h;
        }
    }
    drainage_erode(4, 0.055f);
    thermal_erode(8, 0.038f, 0.09f);
    normalize_grid(-0.55f, 3.35f);
}

void Heightfield::load_mount_st_helens_dem() {
    constexpr int source_size = 1921;
    constexpr float source_center_span_m = 19200.0f;
    constexpr float world_base_height = -0.35f;
    constexpr std::uintmax_t expected_bytes =
        static_cast<std::uintmax_t>(source_size) * source_size * sizeof(float);

    grid_size_ = source_size;
    const std::size_t sample_count =
        static_cast<std::size_t>(source_size) * static_cast<std::size_t>(source_size);
    const std::filesystem::path height_path = std::filesystem::path(GFX_TERRAIN_SOURCE_DIR)
        / "assets" / "dem" / "mount_st_helens_usgs_10m_1921_f32.raw";

    std::error_code file_error;
    if (!std::filesystem::is_regular_file(height_path, file_error) || file_error) {
        throw std::runtime_error("Mount St. Helens DEM RAW asset is missing: " + height_path.string());
    }
    const std::uintmax_t file_bytes = std::filesystem::file_size(height_path, file_error);
    if (file_error || file_bytes != expected_bytes) {
        throw std::runtime_error(
            "Mount St. Helens DEM RAW asset has an invalid size: " + height_path.string()
            + " (expected " + std::to_string(expected_bytes) + " bytes, got "
            + (file_error ? std::string("unavailable") : std::to_string(file_bytes)) + ")");
    }

    grid_.resize(sample_count);
    validity_mask_.clear();

    std::ifstream input(height_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open Mount St. Helens DEM RAW asset: " + height_path.string());
    }
    const std::streamsize byte_count = static_cast<std::streamsize>(sample_count * sizeof(float));
    input.read(reinterpret_cast<char*>(grid_.data()), byte_count);
    if (input.gcount() != byte_count) {
        throw std::runtime_error("Mount St. Helens DEM RAW asset ended before all samples were read.");
    }
    char trailing = 0;
    if (input.read(&trailing, 1)) {
        throw std::runtime_error("Mount St. Helens DEM RAW asset contains unexpected trailing data.");
    }

    for (const float elevation_m : grid_) {
        if (!std::isfinite(elevation_m)) {
            throw std::runtime_error("Mount St. Helens DEM RAW asset contains a non-finite elevation sample.");
        }
    }

    const auto [min_it, max_it] = std::minmax_element(grid_.begin(), grid_.end());
    if (min_it == grid_.end() || max_it == grid_.end() || *max_it <= *min_it) {
        throw std::runtime_error("Mount St. Helens DEM RAW asset has an invalid elevation range.");
    }
    const float min_elevation = *min_it;
    const float max_elevation = *max_it;
    const float meters_to_world = world_size_ / source_center_span_m;
    for (float& elevation_m : grid_) {
        elevation_m = (elevation_m - min_elevation) * meters_to_world + world_base_height;
    }

    std::cout << "Loaded Mount St. Helens DEM RAW: " << source_size << 'x' << source_size
              << ", " << file_bytes << " bytes, elevation " << min_elevation
              << ".." << max_elevation << " m\n";
}

void Heightfield::thermal_erode(int iterations, float talus, float transport) {
    if (grid_.empty()) return;
    std::vector<float> delta(grid_.size());
    constexpr std::array<std::pair<int, int>, 8> neighbors{{
        {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}
    }};

    for (int iteration = 0; iteration < iterations; ++iteration) {
        std::fill(delta.begin(), delta.end(), 0.0f);
        for (int z = 1; z < grid_size_ - 1; ++z) {
            for (int x = 1; x < grid_size_ - 1; ++x) {
                const std::size_t index = static_cast<std::size_t>(z * grid_size_ + x);
                const float current = grid_[index];
                float largest_drop = 0.0f;
                int target_x = x;
                int target_z = z;
                for (const auto& [dx, dz] : neighbors) {
                    const float neighbor = grid_[static_cast<std::size_t>((z + dz) * grid_size_ + (x + dx))];
                    const float drop = current - neighbor;
                    if (drop > largest_drop) {
                        largest_drop = drop;
                        target_x = x + dx;
                        target_z = z + dz;
                    }
                }
                if (largest_drop <= talus) continue;
                const float amount = transport * (largest_drop - talus);
                delta[index] -= amount;
                delta[static_cast<std::size_t>(target_z * grid_size_ + target_x)] += amount;
            }
        }
        for (std::size_t i = 0; i < grid_.size(); ++i) grid_[i] += delta[i];
    }
}

void Heightfield::drainage_erode(int passes, float strength) {
    if (grid_.empty() || passes <= 0 || strength <= 0.0f) return;
    const std::size_t count = grid_.size();
    std::vector<std::size_t> order(count);
    std::vector<float> flow(count, 1.0f);
    std::vector<int> receiver(count, -1);
    std::vector<float> delta(count, 0.0f);
    std::iota(order.begin(), order.end(), 0);

    constexpr std::array<std::pair<int, int>, 8> neighbors{{
        {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}
    }};

    for (int pass = 0; pass < passes; ++pass) {
        std::fill(flow.begin(), flow.end(), 1.0f);
        std::fill(receiver.begin(), receiver.end(), -1);
        std::fill(delta.begin(), delta.end(), 0.0f);
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return grid_[a] > grid_[b];
        });

        for (int z = 1; z < grid_size_ - 1; ++z) {
            for (int x = 1; x < grid_size_ - 1; ++x) {
                const std::size_t index = static_cast<std::size_t>(z * grid_size_ + x);
                float lowest = grid_[index];
                int target = -1;
                for (const auto& [dx, dz] : neighbors) {
                    const std::size_t ni = static_cast<std::size_t>((z + dz) * grid_size_ + (x + dx));
                    if (grid_[ni] < lowest) {
                        lowest = grid_[ni];
                        target = static_cast<int>(ni);
                    }
                }
                receiver[index] = target;
            }
        }

        for (const std::size_t index : order) {
            const int target = receiver[index];
            if (target >= 0) flow[static_cast<std::size_t>(target)] += flow[index];
        }

        const float flow_norm = std::log1p(static_cast<float>(count) * 0.08f);
        for (int z = 1; z < grid_size_ - 1; ++z) {
            for (int x = 1; x < grid_size_ - 1; ++x) {
                const std::size_t index = static_cast<std::size_t>(z * grid_size_ + x);
                const int target = receiver[index];
                if (target < 0) continue;
                const float drop = grid_[index] - grid_[static_cast<std::size_t>(target)];
                const float normalized_flow = std::clamp(std::log1p(flow[index]) / flow_norm, 0.0f, 1.0f);
                const float slope_factor = smoothstep01(drop / 0.055f);
                const float carve = strength * std::pow(normalized_flow, 1.45f) * slope_factor;
                delta[index] -= carve;
                delta[static_cast<std::size_t>(target)] -= carve * 0.18f;
            }
        }
        for (std::size_t i = 0; i < count; ++i) grid_[i] += delta[i];
    }
}

void Heightfield::normalize_grid(float minimum, float maximum) {
    if (grid_.empty()) return;
    const auto [min_it, max_it] = std::minmax_element(grid_.begin(), grid_.end());
    const float source_min = *min_it;
    const float source_max = *max_it;
    const float range = std::max(source_max - source_min, 1.0e-6f);
    for (float& h : grid_) {
        const float t = (h - source_min) / range;
        h = std::lerp(minimum, maximum, t);
    }
}

void Heightfield::update_height_range() {
    if (grid_.empty()) {
        min_height_ = std::numeric_limits<float>::max();
        max_height_ = std::numeric_limits<float>::lowest();
        constexpr int samples = 129;
        const float half = world_size_ * 0.5f;
        for (int z = 0; z < samples; ++z) {
            const float tz = static_cast<float>(z) / static_cast<float>(samples - 1);
            const float wz = std::lerp(-half, half, tz);
            for (int x = 0; x < samples; ++x) {
                const float tx = static_cast<float>(x) / static_cast<float>(samples - 1);
                const float wx = std::lerp(-half, half, tx);
                const float h = height(wx, wz);
                min_height_ = std::min(min_height_, h);
                max_height_ = std::max(max_height_, h);
            }
        }
        return;
    }
    const auto [min_it, max_it] = std::minmax_element(grid_.begin(), grid_.end());
    min_height_ = *min_it;
    max_height_ = *max_it;
}

}
