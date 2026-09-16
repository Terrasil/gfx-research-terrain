#include <gfx/terrain/TerrainTextures.hpp>

#include <array>

#include <glad/gl.h>

namespace gfx::terrain {

void TerrainTextures::clear() {
    for (auto& texture : textures_) texture.clear();
    has_external_assets_ = false;
}

bool TerrainTextures::load_or_fallback(
    gfx::research::Texture2D& texture,
    const std::filesystem::path& path,
    const bool srgb,
    const std::array<unsigned char, 4> fallback) {

    if (texture.load(path, srgb, true)) return true;

    texture.create(1, 1, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, 1);
    texture.upload(0, GL_RGBA, GL_UNSIGNED_BYTE, fallback.data());
    texture.set_wrap(GL_REPEAT, GL_REPEAT);
    texture.set_filters(GL_LINEAR, GL_LINEAR);
    return false;
}

bool TerrainTextures::load(const std::filesystem::path& root) {
    clear();

    struct MaterialFiles {
        const char* directory;
        std::array<unsigned char, 4> fallback_color;
        unsigned char fallback_roughness;
    };

    constexpr MaterialFiles materials[] = {
        {"grass", {106, 112, 69, 255}, 205},
        {"dirt", {91, 66, 45, 255}, 220},
        {"rock", {116, 109, 99, 255}, 205},
        {"sand", {126, 111, 87, 255}, 225}
    };

    bool all_found = true;
    for (int material = 0; material < MaterialCount; ++material) {
        const MaterialFiles& files = materials[material];
        const std::filesystem::path dir = root / files.directory;

        all_found &= load_or_fallback(
            textures_[texture_index(material, Diffuse)],
            dir / "albedo.jpg",
            true,
            files.fallback_color);
        all_found &= load_or_fallback(
            textures_[texture_index(material, Normal)],
            dir / "normal.png",
            false,
            {128, 128, 255, 255});
        all_found &= load_or_fallback(
            textures_[texture_index(material, Roughness)],
            dir / "roughness.png",
            false,
            {files.fallback_roughness, files.fallback_roughness, files.fallback_roughness, 255});
        all_found &= load_or_fallback(
            textures_[texture_index(material, Height)],
            dir / "height.png",
            false,
            {128, 128, 128, 255});
    }

    has_external_assets_ = all_found;
    return all_found;
}

void TerrainTextures::bind() const {
    for (std::size_t unit = 0; unit < textures_.size(); ++unit) {
        textures_[unit].bind(static_cast<unsigned int>(unit));
    }
}

}
