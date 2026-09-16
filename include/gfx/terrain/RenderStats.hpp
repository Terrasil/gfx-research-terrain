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
    std::size_t adaptive_leaf_count = 0;
    std::size_t morphed_vertices = 0;
    std::size_t saturated_regions = 0;
    double mean_refinement_level = 0.0;
    double adaptation_cpu_ms = 0.0;
    std::size_t topology_open_internal_edges = 0;
    std::size_t topology_nonmanifold_edges = 0;
    std::array<std::size_t, max_lod_count> lod_histogram{};
};

}
