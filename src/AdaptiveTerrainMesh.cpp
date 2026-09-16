#include <gfx/terrain/AdaptiveTerrainMesh.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numbers>

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace gfx::terrain {

static float Smoothstep01(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

AdaptiveTerrainMesh::~AdaptiveTerrainMesh() {
    destroy_gpu_objects();
}

bool AdaptiveTerrainMesh::build(
    const Heightfield& heightfield,
    float world_size,
    int max_level,
    int error_samples_per_axis) {

    if (max_level < 1 || max_level > 10 || error_samples_per_axis < 3) return false;

    max_level_ = max_level;
    grid_cells_ = 1 << max_level_;
    world_size_ = world_size;
    half_world_ = world_size * 0.5f;
    nodes_.clear();
    leaves_.clear();
    coverage_.clear();
    vertices_.clear();
    indices_.clear();

    cache_reference_grid(heightfield);
    update_cache_valid_ = false;

    std::size_t node_count = 0;
    std::size_t level_nodes = 1;
    for (int level = 0; level <= max_level_; ++level) {
        node_count += level_nodes;
        level_nodes *= 4;
    }
    nodes_.reserve(node_count);
    build_node(heightfield, -1, 0, 0, 0, grid_cells_, error_samples_per_axis);
    split_history_.assign(nodes_.size(), 0);
    ensure_gpu_objects();
    return true;
}

void AdaptiveTerrainMesh::cache_reference_grid(const Heightfield& heightfield) {
    const int side = grid_cells_ + 1;
    const std::size_t count = static_cast<std::size_t>(side * side);
    reference_heights_.resize(count);
    reference_vertex_normals_.resize(count);
    const float step = world_size_ / static_cast<float>(grid_cells_);

    for (int gz = 0; gz <= grid_cells_; ++gz) {
        const float z = -half_world_ + static_cast<float>(gz) * step;
        for (int gx = 0; gx <= grid_cells_; ++gx) {
            const float x = -half_world_ + static_cast<float>(gx) * step;
            reference_heights_[static_cast<std::size_t>(gz * side + gx)] = heightfield.height(x, z);
        }
    }

    for (int gz = 0; gz <= grid_cells_; ++gz) {
        for (int gx = 0; gx <= grid_cells_; ++gx) {
            const int x0 = std::max(0, gx - 1);
            const int x1 = std::min(grid_cells_, gx + 1);
            const int z0 = std::max(0, gz - 1);
            const int z1 = std::min(grid_cells_, gz + 1);
            const float dx = (cached_height(x1, gz) - cached_height(x0, gz)) /
                std::max(static_cast<float>(x1 - x0) * step, 1.0e-6f);
            const float dz = (cached_height(gx, z1) - cached_height(gx, z0)) /
                std::max(static_cast<float>(z1 - z0) * step, 1.0e-6f);
            reference_vertex_normals_[static_cast<std::size_t>(gz * side + gx)] =
                glm::normalize(glm::vec3{-dx, 1.0f, -dz});
        }
    }
}

void AdaptiveTerrainMesh::reset_history() {
    std::fill(split_history_.begin(), split_history_.end(), 0);
    update_cache_valid_ = false;
}

int AdaptiveTerrainMesh::build_node(
    const Heightfield& heightfield,
    int parent,
    int level,
    int gx,
    int gz,
    int size,
    int error_samples_per_axis) {

    const int index = static_cast<int>(nodes_.size());
    nodes_.push_back({});
    Node& node = nodes_.back();
    node.parent = parent;
    node.level = level;
    node.gx = gx;
    node.gz = gz;
    node.size = size;
    measure_node(node, heightfield, error_samples_per_axis);

    if (level >= max_level_) return index;

    const int half = size / 2;
    std::array<int, 4> children{};
    children[0] = build_node(heightfield, index, level + 1, gx, gz, half, error_samples_per_axis);
    children[1] = build_node(heightfield, index, level + 1, gx + half, gz, half, error_samples_per_axis);
    children[2] = build_node(heightfield, index, level + 1, gx, gz + half, half, error_samples_per_axis);
    children[3] = build_node(heightfield, index, level + 1, gx + half, gz + half, half, error_samples_per_axis);
    nodes_[static_cast<std::size_t>(index)].children = children;
    return index;
}

void AdaptiveTerrainMesh::measure_node(
    Node& node,
    const Heightfield& heightfield,
    int samples_per_axis) const {

    const float step_world = world_size_ / static_cast<float>(grid_cells_);
    const float x0 = -half_world_ + static_cast<float>(node.gx) * step_world;
    const float z0 = -half_world_ + static_cast<float>(node.gz) * step_world;
    const float x1 = -half_world_ + static_cast<float>(node.gx + node.size) * step_world;
    const float z1 = -half_world_ + static_cast<float>(node.gz + node.size) * step_world;

    float max_error = 0.0f;
    float max_normal_error = 0.0f;
    float max_feature = 0.0f;
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();

    for (int iz = 0; iz < samples_per_axis; ++iz) {
        const float v = static_cast<float>(iz) / static_cast<float>(samples_per_axis - 1);
        const float grid_z = static_cast<float>(node.gz) + v * static_cast<float>(node.size);
        const float z = std::lerp(z0, z1, v);
        for (int ix = 0; ix < samples_per_axis; ++ix) {
            const float u = static_cast<float>(ix) / static_cast<float>(samples_per_axis - 1);
            const float grid_x = static_cast<float>(node.gx) + u * static_cast<float>(node.size);
            const float x = std::lerp(x0, x1, u);

            const float truth = reference_height(grid_x, grid_z);
            const float approx = approximate_height(node.level, grid_x, grid_z);
            max_error = std::max(max_error, std::abs(truth - approx));

            const glm::vec3 truth_normal = reference_normal(grid_x, grid_z);
            const glm::vec3 approx_normal = approximate_triangle_normal(node.level, grid_x, grid_z);
            const float dot_value = std::clamp(glm::dot(truth_normal, approx_normal), -1.0f, 1.0f);
            const float angle = std::acos(dot_value) * 180.0f / std::numbers::pi_v<float>;
            max_normal_error = std::max(max_normal_error, angle);
            max_feature = std::max(max_feature, heightfield.feature_strength(x, z));
            min_y = std::min(min_y, truth);
            max_y = std::max(max_y, truth);
        }
    }

    if (node.level == max_level_) {
        max_error = 0.0f;
        max_normal_error = 0.0f;
    }

    node.max_height_error = max_error;
    node.max_normal_error_degrees = max_normal_error;
    node.max_feature_strength = max_feature;
    node.center = {0.5f * (x0 + x1), 0.5f * (min_y + max_y), 0.5f * (z0 + z1)};
    const glm::vec3 half_extent{0.5f * (x1 - x0), 0.5f * (max_y - min_y), 0.5f * (z1 - z0)};
    node.bounding_radius = glm::length(half_extent);
}

float AdaptiveTerrainMesh::projected_geometric_error_px(
    const Node& node,
    const SelectionContext& context) const {

    const glm::vec3 delta = node.center - context.camera_position;
    const double center_distance = std::sqrt(static_cast<double>(glm::dot(delta, delta)));
    const double nearest_distance = std::max(0.05, center_distance - static_cast<double>(node.bounding_radius));
    const double fov = static_cast<double>(context.vertical_fov_degrees) * std::numbers::pi / 180.0;
    const double focal_pixels = static_cast<double>(context.viewport_height) / (2.0 * std::tan(0.5 * fov));
    return static_cast<float>(static_cast<double>(node.max_height_error) * focal_pixels / nearest_distance);
}

float AdaptiveTerrainMesh::node_ratio(
    const Node& node,
    ResearchMethod method,
    const SelectionContext& context) const {

    const glm::vec3 delta = node.center - context.camera_position;
    const double center_distance = std::sqrt(static_cast<double>(glm::dot(delta, delta)));
    const double nearest_distance = std::max(0.05, center_distance - static_cast<double>(node.bounding_radius));
    const double fov = static_cast<double>(context.vertical_fov_degrees) * std::numbers::pi / 180.0;
    const double focal_pixels = static_cast<double>(context.viewport_height) / (2.0 * std::tan(0.5 * fov));

    if (method == ResearchMethod::AdaptiveDistance) {
        const double node_world_size = world_size_ * static_cast<double>(node.size) / static_cast<double>(grid_cells_);
        const double screen_extent = node_world_size * focal_pixels / nearest_distance;
        const double target_leaf_pixels = 18.0 * std::pow(2.0, static_cast<double>(context.distance_lod_bias));
        return static_cast<float>(screen_extent / std::max(target_leaf_pixels, 1.0));
    }

    const double geometric_px = static_cast<double>(projected_geometric_error_px(node, context));
    double controlled_px = geometric_px;
    if (method == ResearchMethod::AdaptiveErrorFeatureAware && geometric_px > 0.0) {
        const double normal_radians = static_cast<double>(node.max_normal_error_degrees) * std::numbers::pi / 180.0;
        const double normal_proxy = static_cast<double>(node.bounding_radius) *
            std::sin(std::clamp(normal_radians, 0.0, std::numbers::pi / 2.0)) * focal_pixels / nearest_distance;
        const double feature = std::clamp(static_cast<double>(node.max_feature_strength), 0.0, 1.0);
        const double weight = std::max(0.0, static_cast<double>(context.feature_weight));
        const double normal_scale = std::clamp(geometric_px / std::max(geometric_px + 1.0, 1.0), 0.0, 1.0);
        controlled_px += weight * (0.25 * feature * geometric_px + 0.05 * normal_proxy * normal_scale);
    }
    return static_cast<float>(controlled_px / std::max(static_cast<double>(context.error_budget_px), 1.0e-6));
}

void AdaptiveTerrainMesh::select_leaves(
    int node_index,
    ResearchMethod method,
    const SelectionContext& context,
    float morph_band,
    bool force_finest,
    RenderStats& stats) {

    Node& node = nodes_[static_cast<std::size_t>(node_index)];
    const float ratio = force_finest ? std::numeric_limits<float>::infinity() : node_ratio(node, method, context);
    const bool can_split = node.level < max_level_ && node.children[0] >= 0;
    const bool was_split = split_history_[static_cast<std::size_t>(node_index)] != 0;
    const float hysteresis = std::clamp(context.hysteresis, 0.0f, 0.45f);
    const float split_threshold = was_split ? 1.0f - hysteresis : 1.0f + hysteresis;
    const bool split = can_split && (force_finest || ratio > split_threshold);
    if (split != was_split) ++stats.lod_transitions;
    split_history_[static_cast<std::size_t>(node_index)] = split ? 1 : 0;

    if (split) {
        for (const int child : node.children) {
            select_leaves(child, method, context, morph_band, force_finest, stats);
        }
        return;
    }

    if (was_split) {
        std::vector<int> stack;
        for (const int child : node.children) {
            if (child >= 0) stack.push_back(child);
        }
        while (!stack.empty()) {
            const int descendant_index = stack.back();
            stack.pop_back();
            Node& descendant = nodes_[static_cast<std::size_t>(descendant_index)];
            split_history_[static_cast<std::size_t>(descendant_index)] = 0;
            for (const int child : descendant.children) {
                if (child >= 0) stack.push_back(child);
            }
        }
    }

    float morph = 1.0f;
    if (!force_finest && node.parent >= 0) {
        const Node& parent = nodes_[static_cast<std::size_t>(node.parent)];
        const float parent_ratio = node_ratio(parent, method, context);
        const float band = std::max(morph_band, 0.01f);
        const float enter_threshold = 1.0f + hysteresis;
        morph = Smoothstep01((parent_ratio - enter_threshold) / band);
    }

    leaves_.push_back({node_index, ratio, morph});
    ++stats.adaptive_leaf_count;
    const int histogram_level = std::clamp(node.level, 0, max_lod_count - 1);
    ++stats.lod_histogram[static_cast<std::size_t>(histogram_level)];
    const double geometric_px = projected_geometric_error_px(node, context);
    stats.max_geometric_error_px = std::max(stats.max_geometric_error_px, geometric_px);
    stats.mean_geometric_error_px += geometric_px;
    if (method == ResearchMethod::AdaptiveError || method == ResearchMethod::AdaptiveErrorFeatureAware) {
        stats.max_controller_error_px = std::max(
            stats.max_controller_error_px,
            static_cast<double>(ratio) * static_cast<double>(context.error_budget_px));
    }
    stats.mean_refinement_level += static_cast<double>(node.level);
    if ((method == ResearchMethod::AdaptiveError || method == ResearchMethod::AdaptiveErrorFeatureAware) &&
        node.level == max_level_ && ratio > 1.0f) {
        ++stats.saturated_regions;
    }
}

void AdaptiveTerrainMesh::build_coverage() {
    coverage_.assign(static_cast<std::size_t>(grid_cells_ * grid_cells_), -1);
    for (std::size_t leaf_index = 0; leaf_index < leaves_.size(); ++leaf_index) {
        const Node& node = nodes_[static_cast<std::size_t>(leaves_[leaf_index].node_index)];
        for (int z = node.gz; z < node.gz + node.size; ++z) {
            const std::size_t row = static_cast<std::size_t>(z * grid_cells_);
            std::fill(
                coverage_.begin() + static_cast<std::ptrdiff_t>(row + static_cast<std::size_t>(node.gx)),
                coverage_.begin() + static_cast<std::ptrdiff_t>(row + static_cast<std::size_t>(node.gx + node.size)),
                static_cast<int>(leaf_index));
        }
    }
}

int AdaptiveTerrainMesh::adjacent_leaf_for_cell(int gx, int gz) const {
    if (gx < 0 || gz < 0 || gx >= grid_cells_ || gz >= grid_cells_) return -1;
    return coverage_[static_cast<std::size_t>(gz * grid_cells_ + gx)];
}

int AdaptiveTerrainMesh::vertex_constraint_level(int gx, int gz, int fallback_level) const {
    int level = fallback_level;
    for (int dz = -1; dz <= 0; ++dz) {
        for (int dx = -1; dx <= 0; ++dx) {
            const int leaf_index = adjacent_leaf_for_cell(gx + dx, gz + dz);
            if (leaf_index < 0) continue;
            const std::size_t leaf_offset = static_cast<std::size_t>(leaf_index);
            const Node& neighbor = nodes_[static_cast<std::size_t>(leaves_[leaf_offset].node_index)];
            level = std::min(level, neighbor.level);
        }
    }
    return level;
}

float AdaptiveTerrainMesh::cached_height(int gx, int gz) const {
    gx = std::clamp(gx, 0, grid_cells_);
    gz = std::clamp(gz, 0, grid_cells_);
    const int side = grid_cells_ + 1;
    return reference_heights_[static_cast<std::size_t>(gz * side + gx)];
}

float AdaptiveTerrainMesh::reference_height(float gx, float gz) const {
    gx = std::clamp(gx, 0.0f, static_cast<float>(grid_cells_));
    gz = std::clamp(gz, 0.0f, static_cast<float>(grid_cells_));
    const int ix = std::min(static_cast<int>(std::floor(gx)), grid_cells_ - 1);
    const int iz = std::min(static_cast<int>(std::floor(gz)), grid_cells_ - 1);
    const float fx = gx - static_cast<float>(ix);
    const float fz = gz - static_cast<float>(iz);
    const float h00 = cached_height(ix, iz);
    const float h10 = cached_height(ix + 1, iz);
    const float h01 = cached_height(ix, iz + 1);
    const float h11 = cached_height(ix + 1, iz + 1);
    if (fx + fz <= 1.0f) return h00 + fx * (h10 - h00) + fz * (h01 - h00);
    return h11 + (1.0f - fx) * (h01 - h11) + (1.0f - fz) * (h10 - h11);
}

glm::vec3 AdaptiveTerrainMesh::reference_normal(float gx, float gz) const {
    return approximate_triangle_normal(max_level_, gx, gz);
}

float AdaptiveTerrainMesh::approximate_height(int level, float gx, float gz) const {
    level = std::clamp(level, 0, max_level_);
    gx = std::clamp(gx, 0.0f, static_cast<float>(grid_cells_));
    gz = std::clamp(gz, 0.0f, static_cast<float>(grid_cells_));
    const int cell_size = 1 << (max_level_ - level);
    const int cells_per_side = grid_cells_ / cell_size;
    const int cell_x = std::min(static_cast<int>(std::floor(gx / static_cast<float>(cell_size))), cells_per_side - 1);
    const int cell_z = std::min(static_cast<int>(std::floor(gz / static_cast<float>(cell_size))), cells_per_side - 1);
    const int x0 = cell_x * cell_size;
    const int z0 = cell_z * cell_size;
    const int x1 = x0 + cell_size;
    const int z1 = z0 + cell_size;
    const float u = (gx - static_cast<float>(x0)) / static_cast<float>(cell_size);
    const float v = (gz - static_cast<float>(z0)) / static_cast<float>(cell_size);
    const float h00 = cached_height(x0, z0);
    const float h10 = cached_height(x1, z0);
    const float h01 = cached_height(x0, z1);
    const float h11 = cached_height(x1, z1);
    if (u + v <= 1.0f) return h00 + u * (h10 - h00) + v * (h01 - h00);
    return h11 + (1.0f - u) * (h01 - h11) + (1.0f - v) * (h10 - h11);
}

glm::vec3 AdaptiveTerrainMesh::approximate_triangle_normal(int level, float gx, float gz) const {
    level = std::clamp(level, 0, max_level_);
    gx = std::clamp(gx, 0.0f, static_cast<float>(grid_cells_));
    gz = std::clamp(gz, 0.0f, static_cast<float>(grid_cells_));
    const int cell_size = 1 << (max_level_ - level);
    const int cells_per_side = grid_cells_ / cell_size;
    const int cell_x = std::min(static_cast<int>(std::floor(gx / static_cast<float>(cell_size))), cells_per_side - 1);
    const int cell_z = std::min(static_cast<int>(std::floor(gz / static_cast<float>(cell_size))), cells_per_side - 1);
    const int x0 = cell_x * cell_size;
    const int z0 = cell_z * cell_size;
    const int x1 = x0 + cell_size;
    const int z1 = z0 + cell_size;
    const float u = (gx - static_cast<float>(x0)) / static_cast<float>(cell_size);
    const float v = (gz - static_cast<float>(z0)) / static_cast<float>(cell_size);
    const glm::vec3 p00 = grid_to_world(static_cast<float>(x0), static_cast<float>(z0), cached_height(x0, z0));
    const glm::vec3 p10 = grid_to_world(static_cast<float>(x1), static_cast<float>(z0), cached_height(x1, z0));
    const glm::vec3 p01 = grid_to_world(static_cast<float>(x0), static_cast<float>(z1), cached_height(x0, z1));
    const glm::vec3 p11 = grid_to_world(static_cast<float>(x1), static_cast<float>(z1), cached_height(x1, z1));
    if (u + v <= 1.0f) return glm::normalize(glm::cross(p01 - p00, p10 - p00));
    return glm::normalize(glm::cross(p10 - p11, p01 - p11));
}

float AdaptiveTerrainMesh::morphed_vertex_height(
    const Leaf& leaf,
    float gx,
    float gz,
    int constraint_level) const {

    const Node& node = nodes_[static_cast<std::size_t>(leaf.node_index)];
    if (constraint_level < node.level) return approximate_height(constraint_level, gx, gz);
    const float fine = approximate_height(node.level, gx, gz);
    if (node.level == 0 || leaf.morph >= 0.9999f) return fine;
    const float coarse = approximate_height(node.level - 1, gx, gz);
    return std::lerp(coarse, fine, leaf.morph);
}

glm::vec3 AdaptiveTerrainMesh::grid_to_world(float gx, float gz, float height) const {
    const float step = world_size_ / static_cast<float>(grid_cells_);
    return {-half_world_ + gx * step, height, -half_world_ + gz * step};
}

void AdaptiveTerrainMesh::append_edge_points(
    const Node& node,
    Edge edge,
    std::vector<std::pair<int, int>>& boundary) const {

    std::vector<std::pair<int, int>> points;
    const auto append_if_new = [&](int gx, int gz) {
        if (points.empty() || points.back().first != gx || points.back().second != gz) points.emplace_back(gx, gz);
    };

    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    const bool reverse = edge == Edge::Right || edge == Edge::Bottom;
    const int begin = vertical ? node.gz : node.gx;
    const int end = begin + node.size;

    auto neighbor_at_segment = [&](int coordinate) {
        if (edge == Edge::Left) return adjacent_leaf_for_cell(node.gx - 1, coordinate);
        if (edge == Edge::Right) return adjacent_leaf_for_cell(node.gx + node.size, coordinate);
        if (edge == Edge::Top) return adjacent_leaf_for_cell(coordinate, node.gz + node.size);
        return adjacent_leaf_for_cell(coordinate, node.gz - 1);
    };

    auto make_point = [&](int coordinate) {
        if (vertical) return std::pair<int, int>{edge == Edge::Left ? node.gx : node.gx + node.size, coordinate};
        return std::pair<int, int>{coordinate, edge == Edge::Bottom ? node.gz : node.gz + node.size};
    };

    append_if_new(make_point(begin).first, make_point(begin).second);
    int previous_leaf = neighbor_at_segment(begin);
    for (int coordinate = begin + 1; coordinate < end; ++coordinate) {
        const int current_leaf = neighbor_at_segment(coordinate);
        if (current_leaf != previous_leaf) {
            const auto point = make_point(coordinate);
            append_if_new(point.first, point.second);
        }
        previous_leaf = current_leaf;
    }
    const auto final_point = make_point(end);
    append_if_new(final_point.first, final_point.second);

    if (reverse) std::reverse(points.begin(), points.end());
    for (const auto& point : points) {
        if (!boundary.empty() && boundary.back() == point) continue;
        boundary.push_back(point);
    }
}

void AdaptiveTerrainMesh::append_vertex(
    const Leaf& leaf,
    float gx,
    float gz,
    int constraint_level,
    bool allow_morph) {

    const Node& node = nodes_[static_cast<std::size_t>(leaf.node_index)];
    const float y = allow_morph
        ? morphed_vertex_height(leaf, gx, gz, constraint_level)
        : approximate_height(std::min(node.level, constraint_level), gx, gz);

    const float clamped_x = std::clamp(gx, 0.0f, static_cast<float>(grid_cells_));
    const float clamped_z = std::clamp(gz, 0.0f, static_cast<float>(grid_cells_));
    const int x0 = std::clamp(static_cast<int>(std::floor(clamped_x)), 0, grid_cells_);
    const int z0 = std::clamp(static_cast<int>(std::floor(clamped_z)), 0, grid_cells_);
    const int x1 = std::min(x0 + 1, grid_cells_);
    const int z1 = std::min(z0 + 1, grid_cells_);
    const float tx = clamped_x - static_cast<float>(x0);
    const float tz = clamped_z - static_cast<float>(z0);
    const int side = grid_cells_ + 1;
    const auto normal_at = [&](int x, int z) -> const glm::vec3& {
        return reference_vertex_normals_[static_cast<std::size_t>(z * side + x)];
    };
    const glm::vec3 n0 = glm::mix(normal_at(x0, z0), normal_at(x1, z0), tx);
    const glm::vec3 n1 = glm::mix(normal_at(x0, z1), normal_at(x1, z1), tx);

    Vertex vertex;
    vertex.position = grid_to_world(gx, gz, y);
    vertex.normal = glm::normalize(glm::mix(n0, n1, tz));
    vertex.uv = {gx / static_cast<float>(grid_cells_), gz / static_cast<float>(grid_cells_)};
    vertex.level = static_cast<float>(node.level);
    vertex.error_ratio = leaf.error_ratio;
    vertices_.push_back(vertex);
}

void AdaptiveTerrainMesh::build_geometry(RenderStats& stats) {
    vertices_.clear();
    indices_.clear();
    vertices_.reserve(leaves_.size() * 7);
    indices_.reserve(leaves_.size() * 12);

    double level_sum = 0.0;
    std::vector<std::pair<int, int>> boundary;
    boundary.reserve(32);

    for (const Leaf& leaf : leaves_) {
        const Node& node = nodes_[static_cast<std::size_t>(leaf.node_index)];
        boundary.clear();
        append_edge_points(node, Edge::Left, boundary);
        append_edge_points(node, Edge::Top, boundary);
        append_edge_points(node, Edge::Right, boundary);
        append_edge_points(node, Edge::Bottom, boundary);
        if (boundary.size() > 1 && boundary.front() == boundary.back()) boundary.pop_back();
        if (boundary.size() < 3) continue;

        if (boundary.size() == 4 && leaf.morph >= 0.9999f) {
            const std::uint32_t base = static_cast<std::uint32_t>(vertices_.size());
            for (const auto& [gx, gz] : boundary) {
                const int constraint_level = vertex_constraint_level(gx, gz, node.level);
                append_vertex(leaf, static_cast<float>(gx), static_cast<float>(gz), constraint_level, false);
            }
            indices_.insert(indices_.end(), {
                base, base + 1, base + 3,
                base + 3, base + 1, base + 2
            });
        } else {
            const std::uint32_t center_index = static_cast<std::uint32_t>(vertices_.size());
            const float center_x = static_cast<float>(node.gx) + 0.5f * static_cast<float>(node.size);
            const float center_z = static_cast<float>(node.gz) + 0.5f * static_cast<float>(node.size);
            append_vertex(leaf, center_x, center_z, node.level, true);

            const std::uint32_t boundary_base = static_cast<std::uint32_t>(vertices_.size());
            for (const auto& [gx, gz] : boundary) {
                const int constraint_level = vertex_constraint_level(gx, gz, node.level);
                append_vertex(leaf, static_cast<float>(gx), static_cast<float>(gz), constraint_level, false);
            }

            for (std::size_t i = 0; i < boundary.size(); ++i) {
                const std::uint32_t a = boundary_base + static_cast<std::uint32_t>(i);
                const std::uint32_t b = boundary_base + static_cast<std::uint32_t>((i + 1) % boundary.size());
                indices_.insert(indices_.end(), {center_index, a, b});
            }
            stats.morphed_vertices += boundary.size() + 1;
        }

        level_sum += static_cast<double>(node.level);
    }

    stats.triangles = indices_.size() / 3;
    if (!leaves_.empty()) stats.mean_refinement_level = level_sum / static_cast<double>(leaves_.size());
}

void AdaptiveTerrainMesh::ensure_gpu_objects() {
    if (vao_ != 0) return;
    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &vertex_buffer_);
    glCreateBuffers(1, &index_buffer_);

    glVertexArrayVertexBuffer(vao_, 0, vertex_buffer_, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao_, index_buffer_);

    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(vao_, 0, 0);
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribBinding(vao_, 1, 0);
    glEnableVertexArrayAttrib(vao_, 2);
    glVertexArrayAttribFormat(vao_, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    glVertexArrayAttribBinding(vao_, 2, 0);
    glEnableVertexArrayAttrib(vao_, 3);
    glVertexArrayAttribFormat(vao_, 3, 1, GL_FLOAT, GL_FALSE, offsetof(Vertex, level));
    glVertexArrayAttribBinding(vao_, 3, 0);
    glEnableVertexArrayAttrib(vao_, 4);
    glVertexArrayAttribFormat(vao_, 4, 1, GL_FLOAT, GL_FALSE, offsetof(Vertex, error_ratio));
    glVertexArrayAttribBinding(vao_, 4, 0);
}

void AdaptiveTerrainMesh::upload() {
    ensure_gpu_objects();
    const std::size_t vertex_bytes = vertices_.size() * sizeof(Vertex);
    const std::size_t index_bytes = indices_.size() * sizeof(std::uint32_t);

    if (vertex_bytes > vertex_buffer_capacity_) {
        vertex_buffer_capacity_ = std::max(vertex_bytes, std::max<std::size_t>(vertex_buffer_capacity_ * 2, 64 * 1024));
        glNamedBufferData(vertex_buffer_, static_cast<GLsizeiptr>(vertex_buffer_capacity_), nullptr, GL_STREAM_DRAW);
    }
    if (index_bytes > index_buffer_capacity_) {
        index_buffer_capacity_ = std::max(index_bytes, std::max<std::size_t>(index_buffer_capacity_ * 2, 32 * 1024));
        glNamedBufferData(index_buffer_, static_cast<GLsizeiptr>(index_buffer_capacity_), nullptr, GL_STREAM_DRAW);
    }
    if (vertex_bytes > 0) {
        glNamedBufferSubData(vertex_buffer_, 0, static_cast<GLsizeiptr>(vertex_bytes), vertices_.data());
    }
    if (index_bytes > 0) {
        glNamedBufferSubData(index_buffer_, 0, static_cast<GLsizeiptr>(index_bytes), indices_.data());
    }
    index_count_ = indices_.size();
}

void AdaptiveTerrainMesh::destroy_gpu_objects() {
    if (index_buffer_ != 0) glDeleteBuffers(1, &index_buffer_);
    if (vertex_buffer_ != 0) glDeleteBuffers(1, &vertex_buffer_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
    vertex_buffer_ = 0;
    index_buffer_ = 0;
    index_count_ = 0;
    vertex_buffer_capacity_ = 0;
    index_buffer_capacity_ = 0;
}

void AdaptiveTerrainMesh::update(
    const Heightfield& heightfield,
    ResearchMethod method,
    const SelectionContext& context,
    float morph_band,
    bool force_finest,
    RenderStats& stats,
    bool allow_rebuild) {

    static_cast<void>(heightfield);

    const auto close_float = [](float left, float right) {
        return std::abs(left - right) <= 1.0e-5f;
    };
    const bool same_context = update_cache_valid_
        && cached_method_ == method
        && cached_force_finest_ == force_finest
        && close_float(cached_morph_band_, morph_band)
        && close_float(cached_context_.camera_position.x, context.camera_position.x)
        && close_float(cached_context_.camera_position.y, context.camera_position.y)
        && close_float(cached_context_.camera_position.z, context.camera_position.z)
        && cached_context_.viewport_height == context.viewport_height
        && close_float(cached_context_.vertical_fov_degrees, context.vertical_fov_degrees)
        && close_float(cached_context_.error_budget_px, context.error_budget_px)
        && close_float(cached_context_.feature_weight, context.feature_weight)
        && close_float(cached_context_.hysteresis, context.hysteresis)
        && cached_context_.distance_lod_bias == context.distance_lod_bias;
    if (same_context) {
        stats = cached_stats_;
        stats.lod_transitions = 0;
        return;
    }
    if (!allow_rebuild && update_cache_valid_) {
        stats = cached_stats_;
        stats.lod_transitions = 0;
        return;
    }

    leaves_.clear();
    stats = {};
    select_leaves(0, method, context, morph_band, force_finest, stats);
    if (stats.adaptive_leaf_count > 0) {
        stats.mean_geometric_error_px /= static_cast<double>(stats.adaptive_leaf_count);
    }
    build_coverage();
    build_geometry(stats);
    upload();

    cached_method_ = method;
    cached_context_ = context;
    cached_morph_band_ = morph_band;
    cached_force_finest_ = force_finest;
    cached_stats_ = stats;
    update_cache_valid_ = true;
}


TopologyValidation AdaptiveTerrainMesh::validate_topology() const {
    TopologyValidation result;
    if (indices_.empty() || vertices_.empty()) return result;

    constexpr double quantization = 1000000.0;
    using PointKey = std::array<std::int64_t, 3>;
    using EdgeKey = std::array<std::int64_t, 6>;
    std::map<EdgeKey, int> edge_counts;

    const auto point_key = [](const glm::vec3& p) {
        return PointKey{
            static_cast<std::int64_t>(std::llround(static_cast<double>(p.x) * quantization)),
            static_cast<std::int64_t>(std::llround(static_cast<double>(p.y) * quantization)),
            static_cast<std::int64_t>(std::llround(static_cast<double>(p.z) * quantization))
        };
    };
    const auto edge_key = [&](std::uint32_t first, std::uint32_t second) {
        PointKey a = point_key(vertices_.at(first).position);
        PointKey b = point_key(vertices_.at(second).position);
        if (b < a) std::swap(a, b);
        return EdgeKey{a[0], a[1], a[2], b[0], b[1], b[2]};
    };

    for (std::size_t i = 0; i + 2 < indices_.size(); i += 3) {
        const std::uint32_t a = indices_[i];
        const std::uint32_t b = indices_[i + 1];
        const std::uint32_t c = indices_[i + 2];
        ++edge_counts[edge_key(a, b)];
        ++edge_counts[edge_key(b, c)];
        ++edge_counts[edge_key(c, a)];
    }

    const std::int64_t half = static_cast<std::int64_t>(
        std::llround(static_cast<double>(half_world_) * quantization));
    const auto is_outer_edge = [&](const EdgeKey& edge) {
        const std::int64_t ax = edge[0];
        const std::int64_t az = edge[2];
        const std::int64_t bx = edge[3];
        const std::int64_t bz = edge[5];
        return (ax == -half && bx == -half)
            || (ax == half && bx == half)
            || (az == -half && bz == -half)
            || (az == half && bz == half);
    };

    for (const auto& [edge, count] : edge_counts) {
        if (count == 1) {
            if (!is_outer_edge(edge)) ++result.open_internal_edges;
        } else if (count != 2) {
            ++result.nonmanifold_edges;
        }
    }
    return result;
}

void AdaptiveTerrainMesh::draw() const {
    if (vao_ == 0 || index_count_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count_), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

}
