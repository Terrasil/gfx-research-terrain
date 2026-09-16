#pragma once

#include <gfx/terrain/ErrorController.hpp>
#include <gfx/terrain/Heightfield.hpp>
#include <gfx/terrain/RenderStats.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace gfx::terrain {

struct TopologyValidation {
    std::size_t open_internal_edges = 0;
    std::size_t nonmanifold_edges = 0;

    [[nodiscard]] bool passed() const {
        return open_internal_edges == 0 && nonmanifold_edges == 0;
    }
};

class AdaptiveTerrainMesh {
public:
    AdaptiveTerrainMesh() = default;
    ~AdaptiveTerrainMesh();

    AdaptiveTerrainMesh(const AdaptiveTerrainMesh&) = delete;
    AdaptiveTerrainMesh& operator=(const AdaptiveTerrainMesh&) = delete;

    bool build(const Heightfield& heightfield, float world_size, int max_level, int error_samples_per_axis = 5);
    void reset_history();

    void update(
        const Heightfield& heightfield,
        ResearchMethod method,
        const SelectionContext& context,
        float morph_band,
        bool force_finest,
        RenderStats& stats,
        bool allow_rebuild = true);

    void draw() const;

    [[nodiscard]] std::size_t triangle_count() const { return index_count_ / 3; }
    [[nodiscard]] std::size_t leaf_count() const { return leaves_.size(); }
    [[nodiscard]] int max_level() const { return max_level_; }
    [[nodiscard]] bool has_cached_geometry() const { return update_cache_valid_; }
    [[nodiscard]] TopologyValidation validate_topology() const;

private:
    struct Node {
        int parent = -1;
        std::array<int, 4> children{{-1, -1, -1, -1}};
        int level = 0;
        int gx = 0;
        int gz = 0;
        int size = 0;
        float max_height_error = 0.0f;
        float max_normal_error_degrees = 0.0f;
        float max_feature_strength = 0.0f;
        glm::vec3 center{};
        float bounding_radius = 0.0f;
    };

    struct Leaf {
        int node_index = -1;
        float error_ratio = 0.0f;
        float morph = 1.0f;
    };

    struct Vertex {
        glm::vec3 position{};
        glm::vec3 normal{};
        glm::vec2 uv{};
        float level = 0.0f;
        float error_ratio = 0.0f;
    };

    enum class Edge {
        Left,
        Top,
        Right,
        Bottom
    };

    int build_node(
        const Heightfield& heightfield,
        int parent,
        int level,
        int gx,
        int gz,
        int size,
        int error_samples_per_axis);

    void cache_reference_grid(const Heightfield& heightfield);
    void measure_node(Node& node, const Heightfield& heightfield, int samples_per_axis) const;
    void select_leaves(
        int node_index,
        ResearchMethod method,
        const SelectionContext& context,
        float morph_band,
        bool force_finest,
        RenderStats& stats);
    void build_coverage();
    void build_geometry(RenderStats& stats);
    void append_edge_points(const Node& node, Edge edge, std::vector<std::pair<int, int>>& boundary) const;
    void upload();

    [[nodiscard]] float node_ratio(
        const Node& node,
        ResearchMethod method,
        const SelectionContext& context) const;
    [[nodiscard]] float projected_geometric_error_px(
        const Node& node,
        const SelectionContext& context) const;
    [[nodiscard]] float reference_height(float gx, float gz) const;
    [[nodiscard]] glm::vec3 reference_normal(float gx, float gz) const;
    [[nodiscard]] float approximate_height(int level, float gx, float gz) const;
    [[nodiscard]] glm::vec3 approximate_triangle_normal(int level, float gx, float gz) const;
    [[nodiscard]] int vertex_constraint_level(int gx, int gz, int fallback_level) const;
    [[nodiscard]] float morphed_vertex_height(const Leaf& leaf, float gx, float gz, int constraint_level) const;
    [[nodiscard]] glm::vec3 grid_to_world(float gx, float gz, float height) const;
    [[nodiscard]] float cached_height(int gx, int gz) const;
    [[nodiscard]] int adjacent_leaf_for_cell(int gx, int gz) const;

    void append_vertex(const Leaf& leaf, float gx, float gz, int constraint_level, bool allow_morph);
    void ensure_gpu_objects();
    void destroy_gpu_objects();

    std::vector<Node> nodes_;
    std::vector<std::uint8_t> split_history_;
    std::vector<Leaf> leaves_;
    std::vector<int> coverage_;
    std::vector<float> reference_heights_;
    std::vector<glm::vec3> reference_vertex_normals_;
    std::vector<Vertex> vertices_;
    std::vector<std::uint32_t> indices_;

    int max_level_ = 0;
    int grid_cells_ = 0;
    float world_size_ = 16.0f;
    float half_world_ = 8.0f;

    unsigned int vao_ = 0;
    unsigned int vertex_buffer_ = 0;
    unsigned int index_buffer_ = 0;
    std::size_t index_count_ = 0;
    std::size_t vertex_buffer_capacity_ = 0;
    std::size_t index_buffer_capacity_ = 0;

    bool update_cache_valid_ = false;
    ResearchMethod cached_method_ = ResearchMethod::Reference;
    SelectionContext cached_context_{};
    float cached_morph_band_ = 0.0f;
    bool cached_force_finest_ = false;
    RenderStats cached_stats_{};
};

}
