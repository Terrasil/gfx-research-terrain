#include <gfx/terrain/TerrainResearchApp.hpp>

#include <gfx/terrain/Metrics.hpp>

#include <gfx/research/readback.hpp>
#include <gfx/research/screenshot.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <utility>

#include <glad/gl.h>
#include <imgui.h>

#include <glm/vec4.hpp>

namespace gfx::terrain {

namespace {

std::string gl_string(GLenum name) {
    const auto* value = glGetString(name);
    return value ? reinterpret_cast<const char*>(value) : "unknown";
}

std::string capture_stem(const BenchmarkCase& test_case) {
    return std::string(benchmark_study_name(test_case.study)) + "-" +
        scene_name(test_case.scene) + "-" + method_name(test_case.method) +
        "-d" + std::to_string(static_cast<int>(std::lround(test_case.camera_distance * 10.0f))) +
        "-e" + std::to_string(static_cast<int>(std::lround(test_case.error_budget_px * 100.0f))) +
        "-b" + std::to_string(test_case.distance_lod_bias) +
        "-" + std::to_string(test_case.width) + "x" + std::to_string(test_case.height);
}

}

TerrainResearchApp::TerrainResearchApp(ExperimentConfig config)
    : Application({
          .title = "gfx-research-terrain",
          .width = config.width,
          .height = config.height,
          .vsync = false,
          .debug_context = false,
          .docking = true}),
      experiment_config_(std::move(config)),
      heightfield_(scene_) {}

bool TerrainResearchApp::on_init() {
    const std::filesystem::path root = GFX_TERRAIN_SOURCE_DIR;
    const gfx::research::ShaderFile surface_files[] = {
        {gfx::research::ShaderStage::Vertex, root / "shaders/surface.vert"},
        {gfx::research::ShaderStage::Fragment, root / "shaders/surface.frag"}
    };
    const gfx::research::ShaderFile present_files[] = {
        {gfx::research::ShaderStage::Vertex, root / "shaders/present.vert"},
        {gfx::research::ShaderStage::Fragment, root / "shaders/present.frag"}
    };
    if (!surface_shader_.load(surface_files)) return false;
    if (!present_shader_.load(present_files)) return false;

    camera_.set_target({0.0f, 0.0f, 0.0f});
    camera_.set_distance(12.0f);
    camera_.set_yaw(0.65f);
    camera_.set_pitch(0.55f);
    camera_.set_fov(55.0f);
    camera_.set_clip(0.05f, 250.0f);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    rebuild_scene();
    if (experiment_config_.publication_quick || experiment_config_.publication_suite) {
        benchmark_.configure(experiment_config_);
    }
    return true;
}

void TerrainResearchApp::on_frame(const gfx::research::FrameInfo& frame) {
    ensure_framebuffers(frame.width, frame.height);

    if ((experiment_config_.publication_quick || experiment_config_.publication_suite) && !benchmark_started_) {
        benchmark_.begin(runtime_info(frame.width, frame.height));
        benchmark_started_ = true;
    }

    int presentation_width = frame.width;
    int presentation_height = frame.height;

    if (benchmark_.active()) {
        if (applied_benchmark_case_ != benchmark_.case_index()) {
            apply_benchmark_case(benchmark_.current_case());
            applied_benchmark_case_ = benchmark_.case_index();
        }
        update_benchmark_frame_state();

        const BenchmarkCase& test_case = benchmark_.current_case();
        ensure_framebuffers(test_case.width, test_case.height);
        presentation_width = test_case.width;
        presentation_height = test_case.height;

        last_render_stats_ = render_surface(
            test_case.method,
            method_fbo_,
            test_case.width,
            test_case.height,
            true,
            test_case.force_finest);
        if (gpu_timer_.has_result()) last_gpu_ms_ = gpu_timer_.milliseconds();

        if (benchmark_.should_capture_quality()) capture_quality(test_case);
        if (!benchmark_.in_warmup() && gpu_timer_.has_result()) {
            benchmark_.record_frame(last_gpu_ms_, last_render_stats_, benchmark_.current_camera_distance());
        }

        benchmark_.advance();
        if (benchmark_.finished()) close();
    } else {
        last_render_stats_ = render_surface(
            method_,
            method_fbo_,
            frame.width,
            frame.height,
            true,
            false);
        if (gpu_timer_.has_result()) last_gpu_ms_ = gpu_timer_.milliseconds();
    }

    gfx::research::Framebuffer::bind_default();
    glViewport(0, 0, frame.width, frame.height);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.025f, 0.03f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    (void)presentation_width;
    (void)presentation_height;
    present_texture(method_fbo_.color(), frame.width, frame.height);
    glEnable(GL_DEPTH_TEST);
}

void TerrainResearchApp::on_gui(const gfx::research::FrameInfo& frame) {
    if (benchmark_.active()) return;

    ImGui::Begin("Terrain representation research");
    ImGui::Text("CPU %.3f ms | %.1f FPS", cpu_frame_milliseconds(), fps());
    ImGui::Text("GPU terrain %.3f ms", last_gpu_ms_);
    ImGui::Text("Triangles %zu | transitions %d", last_render_stats_.triangles, last_render_stats_.lod_transitions);
    ImGui::Text("Predicted max geom %.3f px | controller %.3f px",
        last_render_stats_.max_geometric_error_px,
        last_render_stats_.max_controller_error_px);
    ImGui::Separator();

    const char* methods[] = {"Reference", "Distance LOD", "Error bounded", "Error bounded + features"};
    int method_index = static_cast<int>(method_);
    if (ImGui::Combo("Method", &method_index, methods, 4)) {
        method_ = static_cast<ResearchMethod>(method_index);
        patch_set_.reset_history(experiment_config_.lod_count - 1);
    }

    const char* scenes[] = {"Smooth hills", "Sharp ridge", "Mixed frequency", "Cliff band"};
    int scene_index = static_cast<int>(scene_);
    if (ImGui::Combo("Scene", &scene_index, scenes, 4)) {
        scene_ = static_cast<SurfaceScene>(scene_index);
        rebuild_scene();
    }

    ImGui::SliderFloat("Error budget [px]", &experiment_config_.error_budget_px, 0.1f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Feature weight", &experiment_config_.feature_weight, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Hysteresis", &experiment_config_.hysteresis, 0.0f, 0.45f, "%.2f");
    ImGui::SliderInt("Distance LOD bias", &experiment_config_.distance_lod_bias, -2, 3);
    ImGui::Checkbox("Visualize LOD", &experiment_config_.visualize_lod);
    ImGui::Checkbox("Wireframe", &experiment_config_.wireframe);

    ImGui::Separator();
    ImGui::SliderFloat("Camera distance", &camera_.distance(), 2.0f, 45.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderAngle("Camera yaw", &camera_.yaw(), -180.0f, 180.0f);
    ImGui::SliderAngle("Camera pitch", &camera_.pitch(), 5.0f, 85.0f);

    if (ImGui::Button("Reload shaders")) {
        surface_shader_.reload();
        present_shader_.reload();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset LOD history")) patch_set_.reset_history(experiment_config_.lod_count - 1);
    ImGui::SameLine();
    if (ImGui::Button("Screenshot")) {
        std::filesystem::create_directories("results");
        method_fbo_.bind();
        gfx::research::save_framebuffer_png("results/terrain.png", frame.width, frame.height);
    }

    ImGui::Separator();
    for (int lod = 0; lod < experiment_config_.lod_count; ++lod) {
        ImGui::Text("LOD %d patches: %zu", lod, last_render_stats_.lod_histogram[static_cast<std::size_t>(lod)]);
    }
    ImGui::End();
}

void TerrainResearchApp::on_resize(int width, int height) {
    if (benchmark_.active()) return;
    ensure_framebuffers(width, height);
}

void TerrainResearchApp::rebuild_scene() {
    heightfield_.set_scene(scene_);
    patch_set_.build(experiment_config_, heightfield_);
}

void TerrainResearchApp::ensure_framebuffers(int width, int height) {
    if (width <= 0 || height <= 0) return;
    const std::array<gfx::research::ColorAttachmentDesc, 1> colors{{{GL_RGBA16F, GL_LINEAR, GL_LINEAR}}};
    if (!framebuffers_ready_) {
        method_fbo_.create(width, height, colors, true, GL_DEPTH_COMPONENT32F);
        reference_fbo_.create(width, height, colors, true, GL_DEPTH_COMPONENT32F);
        framebuffers_ready_ = true;
        return;
    }
    if (method_fbo_.width() != width || method_fbo_.height() != height) {
        method_fbo_.resize(width, height);
        reference_fbo_.resize(width, height);
    }
}

void TerrainResearchApp::apply_benchmark_case(const BenchmarkCase& test_case) {
    const bool scene_changed = scene_ != test_case.scene;
    scene_ = test_case.scene;
    method_ = test_case.method;
    experiment_config_.error_budget_px = test_case.error_budget_px;
    experiment_config_.distance_lod_bias = test_case.distance_lod_bias;
    experiment_config_.hysteresis = test_case.hysteresis;
    experiment_config_.visualize_lod = false;
    experiment_config_.wireframe = false;

    if (scene_changed) rebuild_scene();
    else patch_set_.reset_history(experiment_config_.lod_count - 1);
    write_lod_error_table(scene_);

    camera_.set_distance(test_case.camera_distance);
    camera_.set_yaw(0.68f);
    camera_.set_pitch(0.52f);
}

void TerrainResearchApp::update_benchmark_frame_state() {
    if (!benchmark_.active()) return;
    camera_.set_distance(benchmark_.current_camera_distance());
}

std::vector<int> TerrainResearchApp::select_lods(
    ResearchMethod method,
    const SelectionContext& context,
    bool force_finest,
    RenderStats& stats) {

    auto& patches = patch_set_.patches();
    std::vector<int> selected;
    selected.resize(patches.size(), experiment_config_.lod_count - 1);

    double geometric_error_sum = 0.0;
    for (std::size_t i = 0; i < patches.size(); ++i) {
        auto& patch = patches[i];
        const int previous = patch.previous_lod;
        const int lod = controller_.select_lod(
            method,
            patch,
            experiment_config_.lod_count,
            context,
            force_finest);
        selected[i] = lod;

        if (lod != previous) ++stats.lod_transitions;
        ++stats.lod_histogram[static_cast<std::size_t>(lod)];
        stats.triangles += patch.errors[static_cast<std::size_t>(lod)].triangle_count;

        const double geometric = controller_.projected_error_px(patch, lod, context, false);
        const bool feature_aware = method == ResearchMethod::ErrorBoundedFeatureAware;
        const double controlled = controller_.projected_error_px(patch, lod, context, feature_aware);
        stats.max_geometric_error_px = std::max(stats.max_geometric_error_px, geometric);
        stats.max_controller_error_px = std::max(stats.max_controller_error_px, controlled);
        geometric_error_sum += geometric;
    }

    if (!patches.empty()) {
        stats.mean_geometric_error_px = geometric_error_sum / static_cast<double>(patches.size());
    }
    return selected;
}

RenderStats TerrainResearchApp::render_surface(
    ResearchMethod method,
    gfx::research::Framebuffer& target,
    int width,
    int height,
    bool time_gpu,
    bool force_finest) {

    target.bind();
    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.035f, 0.045f, 0.065f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, experiment_config_.wireframe ? GL_LINE : GL_FILL);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    surface_shader_.bind();
    surface_shader_.set_mat4("uMvp", camera_.view_projection(aspect));
    surface_shader_.set_vec3("uEye", camera_.position());
    surface_shader_.set_vec3("uLightDir", {-0.35f, -0.85f, 0.40f});
    surface_shader_.set_bool("uVisualizeLod", experiment_config_.visualize_lod);
    surface_shader_.set_int("uScene", static_cast<int>(scene_));

    SelectionContext context;
    context.camera_position = camera_.position();
    context.viewport_height = height;
    context.vertical_fov_degrees = camera_.fov();
    context.error_budget_px = experiment_config_.error_budget_px;
    context.feature_weight = experiment_config_.feature_weight;
    context.hysteresis = experiment_config_.hysteresis;
    context.distance_lod_bias = experiment_config_.distance_lod_bias;

    RenderStats stats;
    const std::vector<int> selected_lods = select_lods(method, context, force_finest, stats);
    const auto& patches = patch_set_.patches();
    const int side = patch_set_.patches_per_side();

    if (time_gpu) gpu_timer_.begin();
    for (std::size_t index = 0; index < patches.size(); ++index) {
        const auto& patch = patches[index];
        const int lod = selected_lods[index];
        const int current_cells = PatchSet::cells_for_lod(experiment_config_.max_cells_per_patch, lod);

        const auto neighbor_cells = [&](int x, int z) {
            if (x < 0 || z < 0 || x >= side || z >= side) return current_cells;
            const std::size_t neighbor_index = static_cast<std::size_t>(z * side + x);
            return PatchSet::cells_for_lod(
                experiment_config_.max_cells_per_patch,
                selected_lods[neighbor_index]);
        };

        const int left = neighbor_cells(patch.grid_x - 1, patch.grid_z);
        const int right = neighbor_cells(patch.grid_x + 1, patch.grid_z);
        const int bottom = neighbor_cells(patch.grid_x, patch.grid_z - 1);
        const int top = neighbor_cells(patch.grid_x, patch.grid_z + 1);

        surface_shader_.set_int("uLod", lod);
        surface_shader_.set_int("uCurrentCells", current_cells);
        surface_shader_.set_vec4("uPatchBounds", {patch.min_x, patch.max_x, patch.min_z, patch.max_z});
        surface_shader_.set_vec4("uNeighborCells", {
            static_cast<float>(left),
            static_cast<float>(right),
            static_cast<float>(bottom),
            static_cast<float>(top)
        });
        patch.meshes[static_cast<std::size_t>(lod)].draw();
    }
    if (time_gpu) gpu_timer_.end();

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    return stats;
}

void TerrainResearchApp::present_texture(unsigned int texture, int width, int height) {
    glViewport(0, 0, width, height);
    present_shader_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    present_shader_.set_int("uColor", 0);
    fullscreen_triangle_.draw();
}

void TerrainResearchApp::capture_quality(const BenchmarkCase& test_case) {
    std::vector<int> saved_history;
    saved_history.reserve(patch_set_.patches().size());
    for (const auto& patch : patch_set_.patches()) saved_history.push_back(patch.previous_lod);

    render_surface(
        ResearchMethod::Reference,
        reference_fbo_,
        test_case.width,
        test_case.height,
        false,
        true);

    for (std::size_t i = 0; i < saved_history.size(); ++i) {
        patch_set_.patches()[i].previous_lod = saved_history[i];
    }

    render_surface(
        test_case.method,
        method_fbo_,
        test_case.width,
        test_case.height,
        false,
        test_case.force_finest);

    const auto ref_color = gfx::research::read_texture_rgba(reference_fbo_.color(), test_case.width, test_case.height);
    const auto test_color = gfx::research::read_texture_rgba(method_fbo_.color(), test_case.width, test_case.height);
    const auto ref_depth = gfx::research::read_texture_depth(reference_fbo_.depth(), test_case.width, test_case.height);
    const auto test_depth = gfx::research::read_texture_depth(method_fbo_.depth(), test_case.width, test_case.height);

    benchmark_.record_quality(
        compare_rgba(ref_color, test_color, ref_depth, test_depth),
        compare_depth(ref_depth, test_depth),
        compare_exact(ref_color, test_color, ref_depth, test_depth));

    if (experiment_config_.save_all_captures || test_case.capture_images) save_case_captures(test_case);
}

void TerrainResearchApp::save_case_captures(const BenchmarkCase& test_case) {
    const std::filesystem::path capture_dir = experiment_config_.output_dir / "captures";
    std::filesystem::create_directories(capture_dir);
    const std::string stem = capture_stem(test_case);

    reference_fbo_.bind();
    gfx::research::save_framebuffer_png(
        capture_dir / (stem + "-reference.png"),
        test_case.width,
        test_case.height);

    method_fbo_.bind();
    gfx::research::save_framebuffer_png(
        capture_dir / (stem + "-method.png"),
        test_case.width,
        test_case.height);
}

void TerrainResearchApp::write_lod_error_table(SurfaceScene scene) {
    const std::size_t scene_index = static_cast<std::size_t>(scene);
    if (scene_index >= scene_error_table_written_.size() || scene_error_table_written_[scene_index]) return;

    std::filesystem::create_directories(experiment_config_.output_dir);
    const std::filesystem::path path = experiment_config_.output_dir / "lod_error.csv";
    const bool exists = std::filesystem::exists(path);
    std::ofstream out(path, std::ios::app);
    if (!exists) {
        out << "scene,patch_x,patch_z,lod,cells,triangles,max_height_error,rms_height_error,"
               "max_normal_error_deg,max_feature_strength,rms_feature_strength\n";
    }

    out << std::setprecision(10);
    for (const auto& patch : patch_set_.patches()) {
        for (int lod = 0; lod < experiment_config_.lod_count; ++lod) {
            const auto& error = patch.errors[static_cast<std::size_t>(lod)];
            out << scene_name(scene) << ','
                << patch.grid_x << ',' << patch.grid_z << ','
                << lod << ','
                << PatchSet::cells_for_lod(experiment_config_.max_cells_per_patch, lod) << ','
                << error.triangle_count << ','
                << error.max_height_error << ','
                << error.rms_height_error << ','
                << error.max_normal_error_degrees << ','
                << error.max_feature_strength << ','
                << error.rms_feature_strength << '\n';
        }
    }
    scene_error_table_written_[scene_index] = true;
}

RuntimeInfo TerrainResearchApp::runtime_info(int width, int height) const {
    RuntimeInfo info;
    info.gl_vendor = gl_string(GL_VENDOR);
    info.gl_renderer = gl_string(GL_RENDERER);
    info.gl_version = gl_string(GL_VERSION);
    info.initial_framebuffer_width = width;
    info.initial_framebuffer_height = height;
    return info;
}

const char* method_name(ResearchMethod method) {
    switch (method) {
    case ResearchMethod::Reference: return "reference";
    case ResearchMethod::DistanceLod: return "distance-lod";
    case ResearchMethod::ErrorBounded: return "error-bounded";
    case ResearchMethod::ErrorBoundedFeatureAware: return "error-bounded-feature";
    }
    return "unknown";
}

const char* scene_name(SurfaceScene scene) {
    switch (scene) {
    case SurfaceScene::SmoothHills: return "smooth-hills";
    case SurfaceScene::SharpRidge: return "sharp-ridge";
    case SurfaceScene::MixedFrequency: return "mixed-frequency";
    case SurfaceScene::CliffBand: return "cliff-band";
    }
    return "unknown";
}

const char* benchmark_mode_name(BenchmarkMode mode) {
    switch (mode) {
    case BenchmarkMode::Static: return "static";
    case BenchmarkMode::Temporal: return "temporal";
    case BenchmarkMode::Null: return "null";
    }
    return "unknown";
}

}
