#pragma once

#include <gfx/research/texture_2d.hpp>

#include <array>
#include <cstddef>
#include <filesystem>

namespace gfx::terrain {

class TerrainTextures {
public:
    TerrainTextures() = default;

    TerrainTextures(const TerrainTextures&) = delete;
    TerrainTextures& operator=(const TerrainTextures&) = delete;

    bool load(const std::filesystem::path& root);
    void bind() const;
    [[nodiscard]] bool has_external_assets() const { return has_external_assets_; }

private:
    enum MapIndex : int { Diffuse = 0, Normal = 1, Roughness = 2, Height = 3, MapCount = 4 };
    enum MaterialIndex : int { Grass = 0, Dirt = 1, Rock = 2, Sand = 3, MaterialCount = 4 };

    static constexpr std::size_t TextureCount = static_cast<std::size_t>(MaterialCount)
        * static_cast<std::size_t>(MapCount);

    static constexpr std::size_t texture_index(int material, int map) {
        return static_cast<std::size_t>(material * MapCount + map);
    }

    void clear();
    bool load_or_fallback(
        gfx::research::Texture2D& texture,
        const std::filesystem::path& path,
        bool srgb,
        std::array<unsigned char, 4> fallback);

    std::array<gfx::research::Texture2D, TextureCount> textures_{};
    bool has_external_assets_ = false;
};

}
