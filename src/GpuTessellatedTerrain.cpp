#include <gfx/terrain/GpuTessellatedTerrain.hpp>

#include <algorithm>

#include <glad/gl.h>

namespace gfx::terrain {

GpuTessellatedTerrain::~GpuTessellatedTerrain() {
    destroy();
}

void GpuTessellatedTerrain::initialize() {
    if (vao_ != 0) return;
    glCreateVertexArrays(1, &vao_);
    glCreateQueries(
        GL_PRIMITIVES_GENERATED,
        static_cast<GLsizei>(primitive_queries_.size()),
        primitive_queries_.data());
}

void GpuTessellatedTerrain::draw(
    const int patches_per_side_input,
    const bool synchronize_count,
    const bool measure_primitives) {
    initialize();
    const int patches_per_side = std::clamp(patches_per_side_input, 1, 128);

    if (!measure_primitives) {
        glPatchParameteri(GL_PATCH_VERTICES, 4);
        glBindVertexArray(vao_);
        glDrawArrays(GL_PATCHES, 0, patches_per_side * patches_per_side * 4);
        glBindVertexArray(0);
        return;
    }

    // Resolve only the previous slot. A pending query is never blindly reused: if its result is
    // not ready, either wait for it in synchronized mode or skip primitive measurement for this draw.
    const unsigned int current_query = query_index_;
    if (query_pending_[current_query]) {
        if (synchronize_count) {
            unsigned int primitives = 0;
            glGetQueryObjectuiv(primitive_queries_[current_query], GL_QUERY_RESULT, &primitives);
            last_triangle_count_ = primitives;
            query_pending_[current_query] = false;
        } else {
            int available = GL_FALSE;
            glGetQueryObjectiv(primitive_queries_[current_query], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_TRUE) {
                unsigned int primitives = 0;
                glGetQueryObjectuiv(primitive_queries_[current_query], GL_QUERY_RESULT, &primitives);
                last_triangle_count_ = primitives;
                query_pending_[current_query] = false;
            } else {
                glPatchParameteri(GL_PATCH_VERTICES, 4);
                glBindVertexArray(vao_);
                glDrawArrays(GL_PATCHES, 0, patches_per_side * patches_per_side * 4);
                glBindVertexArray(0);
                return;
            }
        }
    }

    glPatchParameteri(GL_PATCH_VERTICES, 4);
    glBindVertexArray(vao_);
    glBeginQuery(GL_PRIMITIVES_GENERATED, primitive_queries_[current_query]);
    glDrawArrays(GL_PATCHES, 0, patches_per_side * patches_per_side * 4);
    glEndQuery(GL_PRIMITIVES_GENERATED);

    if (synchronize_count) {
        unsigned int primitives = 0;
        glGetQueryObjectuiv(primitive_queries_[current_query], GL_QUERY_RESULT, &primitives);
        last_triangle_count_ = primitives;
        query_pending_[current_query] = false;
    } else {
        query_pending_[current_query] = true;
    }

    query_index_ = (query_index_ + 1u) % static_cast<unsigned int>(primitive_queries_.size());
    glBindVertexArray(0);
}

void GpuTessellatedTerrain::reset_measurement() {
    if (primitive_queries_[0] != 0) {
        for (std::size_t i = 0; i < primitive_queries_.size(); ++i) {
            if (!query_pending_[i]) continue;
            unsigned int ignored = 0;
            glGetQueryObjectuiv(primitive_queries_[i], GL_QUERY_RESULT, &ignored);
            query_pending_[i] = false;
        }
    }
    query_pending_.fill(false);
    query_index_ = 0;
    last_triangle_count_ = 0;
}

void GpuTessellatedTerrain::destroy() {
    if (primitive_queries_[0] != 0) {
        glDeleteQueries(static_cast<GLsizei>(primitive_queries_.size()), primitive_queries_.data());
    }
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    primitive_queries_ = {};
    query_pending_ = {};
    query_index_ = 0;
    vao_ = 0;
    last_triangle_count_ = 0;
}

}
