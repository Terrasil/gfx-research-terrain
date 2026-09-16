#pragma once

#include <array>
#include <cstddef>

namespace gfx::terrain {

class GpuTessellatedTerrain {
public:
    GpuTessellatedTerrain() = default;
    ~GpuTessellatedTerrain();

    GpuTessellatedTerrain(const GpuTessellatedTerrain&) = delete;
    GpuTessellatedTerrain& operator=(const GpuTessellatedTerrain&) = delete;

    void initialize();
    void draw(int patches_per_side, bool synchronize_count = false, bool measure_primitives = true);
    void reset_measurement();

    [[nodiscard]] std::size_t last_triangle_count() const { return last_triangle_count_; }

private:
    void destroy();

    unsigned int vao_ = 0;
    std::array<unsigned int, 2> primitive_queries_{};
    std::array<bool, 2> query_pending_{};
    unsigned int query_index_ = 0;
    std::size_t last_triangle_count_ = 0;
};

}
