#pragma once

#include <gfx/terrain/PatchSet.hpp>

#include <array>
#include <cstddef>

namespace gfx::terrain {

struct RenderStats {
    std::size_t triangles = 0;
    int lod_transitions = 0;
    double max_geometric_error_px = 0.0;
    double max_controller_error_px = 0.0;
    double mean_geometric_error_px = 0.0;
    std::array<std::size_t, max_lod_count> lod_histogram{};
};

}
