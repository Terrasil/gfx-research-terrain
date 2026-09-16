#include <gfx/terrain/TerrainResearchApp.hpp>

#include <gfx/terrain/Metrics.hpp>

#include <gfx/research/readback.hpp>
#include <gfx/research/screenshot.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#ifndef GFX_TERRAIN_PYTHON_EXECUTABLE
#define GFX_TERRAIN_PYTHON_EXECUTABLE "python"
#endif

namespace gfx::terrain {

namespace {

std::string gl_string(GLenum name) {
    const auto* value = glGetString(name);
    return value ? reinterpret_cast<const char*>(value) : "unknown";
}

bool is_adaptive_method(const ResearchMethod method) {
    return method == ResearchMethod::AdaptiveDistance
        || method == ResearchMethod::AdaptiveError
        || method == ResearchMethod::AdaptiveErrorFeatureAware;
}

bool is_tessellation_method(const ResearchMethod method) {
    return method == ResearchMethod::TessellationDistance
        || method == ResearchMethod::TessellationError
        || method == ResearchMethod::TessellationErrorFeatureAware
        || method == ResearchMethod::TessellationVarianceBaseline
        || method == ResearchMethod::TessellationNormalBound
        || method == ResearchMethod::TessellationContextAware;
}

glm::vec3 light_direction(float azimuth_degrees, float elevation_degrees) {
    const float azimuth = glm::radians(azimuth_degrees);
    const float elevation = glm::radians(elevation_degrees);
    const float horizontal = std::cos(elevation);
    return glm::normalize(glm::vec3{
        horizontal * std::cos(azimuth),
        -std::sin(elevation),
        horizontal * std::sin(azimuth)
    });
}


struct CameraPreset {
    const char* name;
    float target_x;
    float target_z;
    float target_height_offset;
    float distance_scale;
    float yaw;
    float pitch;
};

constexpr std::array<CameraPreset, 13> camera_presets{{
    {"Overview", 0.00f, 0.00f, 0.10f, 0.94f, 0.62f, 0.48f},
    {"Grazing silhouette", 0.00f, 0.00f, 0.08f, 0.82f, 0.58f, 0.14f},
    {"Reverse silhouette", 0.00f, 0.00f, 0.10f, 0.88f, 3.70f, 0.16f},
    {"Valley floor", 0.00f, -0.18f, 0.08f, 0.60f, 0.05f, 0.18f},
    {"Ridge flank", 0.22f, 0.16f, 0.10f, 0.54f, -0.95f, 0.26f},
    {"Cliff face", 0.00f, 0.06f, 0.18f, 0.52f, 3.13f, 0.22f},
    {"River channel", -0.10f, -0.24f, 0.05f, 0.44f, 0.10f, 0.12f},
    {"Canyon rim", 0.18f, -0.05f, 0.14f, 0.48f, -1.28f, 0.34f},
    {"High oblique", 0.00f, 0.00f, 0.06f, 1.02f, 0.72f, 0.92f},
    {"Escarpment overview", 0.00f, 0.04f, 0.12f, 1.42f, 3.02f, 0.45f},
    {"Top down", 0.00f, 0.00f, 0.00f, 1.10f, 0.00f, 1.48f},
    {"Close detail", 0.20f, 0.10f, 0.06f, 0.30f, 0.82f, 0.30f},
    {"Macro surface", -0.18f, 0.12f, 0.035f, 0.18f, 2.18f, 0.18f}
}};

std::string capture_stem(const BenchmarkCase& test_case) {
    if (test_case.study == BenchmarkStudy::RealDemValidation) {
        return std::string("dem-") + method_name(test_case.method) +
            "-d" + std::to_string(static_cast<int>(std::lround(test_case.camera_distance * 10.0f))) +
            "-e" + std::to_string(static_cast<int>(std::lround(test_case.error_budget_px * 100.0f))) +
            "-rm" + std::string(render_mode_name(test_case.benchmark_render_mode)) +
            "-nm" + std::string(shading_normal_mode_name(test_case.shading_normal_mode));
    }
    return std::string(benchmark_study_name(test_case.study)) + "-" +
        scene_name(test_case.scene) + "-" + method_name(test_case.method) +
        "-d" + std::to_string(static_cast<int>(std::lround(test_case.camera_distance * 10.0f))) +
        "-yaw" + std::to_string(static_cast<int>(std::lround(glm::degrees(test_case.camera_yaw)))) +
        "-pitch" + std::to_string(static_cast<int>(std::lround(glm::degrees(test_case.camera_pitch)))) +
        "-fov" + std::to_string(static_cast<int>(std::lround(test_case.camera_fov_degrees))) +
        "-tx" + std::to_string(static_cast<int>(std::lround(test_case.camera_target_x * 100.0f))) +
        "-tz" + std::to_string(static_cast<int>(std::lround(test_case.camera_target_z * 100.0f))) +
        "-e" + std::to_string(static_cast<int>(std::lround(test_case.error_budget_px * 100.0f))) +
        "-n" + std::to_string(static_cast<int>(std::lround(test_case.normal_budget_degrees * 10.0f))) +
        "-rm" + std::string(render_mode_name(test_case.benchmark_render_mode)) +
        "-nm" + std::string(shading_normal_mode_name(test_case.shading_normal_mode)) +
        "-r" + std::to_string(static_cast<int>(std::lround(test_case.scientific_roughness * 100.0f))) +
        "-la" + std::to_string(static_cast<int>(std::lround(test_case.light_azimuth_degrees))) +
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
      heightfield_(SurfaceScene::GlacialValley, experiment_config_.world_size) {}

TerrainResearchApp::~TerrainResearchApp() {
    if (heightfield_texture_ != 0) glDeleteTextures(1, &heightfield_texture_);
    if (heightfield_validity_texture_ != 0) glDeleteTextures(1, &heightfield_validity_texture_);
}

bool TerrainResearchApp::on_init() {
    const std::filesystem::path root = GFX_TERRAIN_SOURCE_DIR;
    const gfx::research::ShaderFile surface_files[] = {
        {gfx::research::ShaderStage::Vertex, root / "shaders/surface.vert"},
        {gfx::research::ShaderStage::Fragment, root / "shaders/surface.frag"}
    };
    const gfx::research::ShaderFile tessellation_files[] = {
        {gfx::research::ShaderStage::Vertex, root / "shaders/tess_surface.vert"},
        {gfx::research::ShaderStage::TessellationControl, root / "shaders/tess_surface.tesc"},
        {gfx::research::ShaderStage::TessellationEvaluation, root / "shaders/tess_surface.tese"},
        {gfx::research::ShaderStage::Fragment, root / "shaders/surface.frag"}
    };
    const gfx::research::ShaderFile present_files[] = {
        {gfx::research::ShaderStage::Vertex, root / "shaders/present.vert"},
        {gfx::research::ShaderStage::Fragment, root / "shaders/present.frag"}
    };
    const gfx::research::ShaderFile difference_files[] = {
        {gfx::research::ShaderStage::Vertex, root / "shaders/present.vert"},
        {gfx::research::ShaderStage::Fragment, root / "shaders/difference.frag"}
    };
    if (!surface_shader_.load(surface_files)) return false;
    if (!tessellation_shader_.load(tessellation_files)) return false;
    if (!present_shader_.load(present_files)) return false;
    if (!difference_shader_.load(difference_files)) return false;

    camera_.set_fov(55.0f);
    camera_.set_clip(0.05f, 250.0f);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    terrain_textures_.load(root / "assets/terrain");
    rebuild_scene();
    if (experiment_config_.validation_suite
        || experiment_config_.publication_quick
        || experiment_config_.publication_suite) {
        benchmark_.configure(experiment_config_);
    }
    return true;
}

void TerrainResearchApp::on_frame(const gfx::research::FrameInfo& frame) {
    ensure_framebuffers(frame.width, frame.height);
    camera_moved_this_frame_ = false;
    if (!benchmark_.active() && !experiment_config_.validation_suite
        && !experiment_config_.publication_quick && !experiment_config_.publication_suite) {
        const glm::vec3 previous_position = camera_.position();
        const glm::vec3 previous_target = camera_.target();
        const float previous_fov = camera_.fov();
        update_camera_controls(frame);
        const float position_delta = glm::length(camera_.position() - previous_position);
        const float target_delta = glm::length(camera_.target() - previous_target);
        camera_moved_this_frame_ = position_delta > 1.0e-5f
            || target_delta > 1.0e-5f
            || std::abs(camera_.fov() - previous_fov) > 1.0e-5f;
        camera_idle_seconds_ = camera_moved_this_frame_
            ? 0.0f
            : camera_idle_seconds_ + frame.delta_seconds;
    }

    if ((experiment_config_.validation_suite || experiment_config_.publication_quick
        || experiment_config_.publication_suite) && !benchmark_started_) {
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
        if (benchmark_.finished()) {
            if (gui_test_status_ == GuiTestStatus::Running && gui_test_suite_ != GuiTestSuite::None) {
                finish_gui_test_suite(frame);
            } else {
                close();
            }
        }
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
    if (benchmark_.active()) {
        if (gui_test_status_ == GuiTestStatus::Running && gui_test_suite_ != GuiTestSuite::None) {
            ImGui::Begin("Automated research tests");
            const char* suite_name = "Doctoral full suite";
            if (gui_test_suite_ == GuiTestSuite::Validation) suite_name = "Validation suite";
            else if (gui_test_suite_ == GuiTestSuite::DoctoralQuick) suite_name = "Doctoral quick suite";
            else if (gui_test_suite_ == GuiTestSuite::PbrFigures) suite_name = "PBR screenshot test suite";
            else if (gui_test_suite_ == GuiTestSuite::RealDemValidation) suite_name = "Real DEM validation suite";
            ImGui::TextUnformatted(suite_name);

            const std::size_t case_count = benchmark_.case_count();
            const std::size_t case_index = benchmark_.case_index();
            const int frames_per_case = std::max(1, benchmark_.warmup_frames() + benchmark_.sample_frames());
            const float case_fraction = static_cast<float>(benchmark_.frame_in_case())
                / static_cast<float>(frames_per_case);
            const float progress = case_count == 0
                ? 0.0f
                : (static_cast<float>(case_index) + case_fraction) / static_cast<float>(case_count);
            ImGui::ProgressBar(std::clamp(progress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
            ImGui::Text("Case %zu / %zu", std::min(case_index + 1, case_count), case_count);
            if (case_count > 0) {
                const BenchmarkCase& test_case = benchmark_.current_case();
                ImGui::Text("Study: %s", benchmark_study_name(test_case.study));
                ImGui::Text("Scene: %s", scene_name(test_case.scene));
                ImGui::Text("Method: %s", method_name(test_case.method));
                ImGui::Text("Frame %d / %d", benchmark_.frame_in_case() + 1, frames_per_case);
            }
            ImGui::TextWrapped("%s", gui_test_message_.c_str());
            if (ImGui::Button("Cancel tests")) finish_gui_test_suite(frame, true);
            ImGui::End();
        }
        return;
    }

    ImGui::Begin("Terrain representation research");
    ImGui::Text("CPU %.3f ms | %.1f FPS", cpu_frame_milliseconds(), fps());
    ImGui::Text("GPU terrain %.3f ms", last_gpu_ms_);
    ImGui::Text("Triangles %zu | transitions %d", last_render_stats_.triangles, last_render_stats_.lod_transitions);
    if (is_adaptive_method(method_)) {
        ImGui::Text("Adaptive leaves %zu | mean level %.2f | morph vertices %zu",
            last_render_stats_.adaptive_leaf_count,
            last_render_stats_.mean_refinement_level,
            last_render_stats_.morphed_vertices);
        ImGui::Text("Saturated finest regions %zu", last_render_stats_.saturated_regions);
    }
    ImGui::Text("LOD/adaptation CPU %.3f ms", last_render_stats_.adaptation_cpu_ms);
    ImGui::Text("Predicted max geom %.3f px | controller %.3f px",
        last_render_stats_.max_geometric_error_px,
        last_render_stats_.max_controller_error_px);
    const bool error_method = method_ == ResearchMethod::ErrorBounded
        || method_ == ResearchMethod::ErrorBoundedFeatureAware
        || method_ == ResearchMethod::AdaptiveError
        || method_ == ResearchMethod::AdaptiveErrorFeatureAware
        || method_ == ResearchMethod::TessellationError
        || method_ == ResearchMethod::TessellationErrorFeatureAware;
    if (error_method && last_render_stats_.saturated_regions > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.34f, 0.18f, 1.0f),
            "Budget saturated in %zu finest regions", last_render_stats_.saturated_regions);
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Automated research tests");
    if (ImGui::Button("Run validation tests")) {
        start_gui_test_suite(GuiTestSuite::Validation, frame);
    }
    ImGui::SameLine();
    if (ImGui::Button("Run doctoral quick")) {
        start_gui_test_suite(GuiTestSuite::DoctoralQuick, frame);
    }
    ImGui::SameLine();
    if (ImGui::Button("Run doctoral full")) {
        start_gui_test_suite(GuiTestSuite::DoctoralFull, frame);
    }
    if (ImGui::Button("Run PBR screenshot tests")) {
        start_gui_test_suite(GuiTestSuite::PbrFigures, frame);
    }
    ImGui::SameLine();
    if (ImGui::Button("Run real DEM tests")) {
        start_gui_test_suite(GuiTestSuite::RealDemValidation, frame);
    }

    if (gui_test_status_ == GuiTestStatus::Passed) {
        ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.35f, 1.0f), "PASSED: %s", gui_test_message_.c_str());
    } else if (gui_test_status_ == GuiTestStatus::Failed) {
        ImGui::TextColored(ImVec4(1.0f, 0.30f, 0.20f, 1.0f), "FAILED: %s", gui_test_message_.c_str());
    } else if (gui_test_status_ == GuiTestStatus::Cancelled) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.20f, 1.0f), "CANCELLED: %s", gui_test_message_.c_str());
    } else {
        ImGui::TextDisabled("%s", gui_test_message_.c_str());
    }
    if (!gui_test_output_dir_.empty()) {
        ImGui::TextWrapped("Results: %s", gui_test_output_dir_.string().c_str());
    }
    if (!gui_validation_passed_) {
        ImGui::TextDisabled("Validation has not been run in this session; benchmark buttons remain available.");
    }
    ImGui::Separator();

    const char* methods[] = {
        "Reference (dense)",
        "Patch distance LOD",
        "Patch: sampled error control",
        "Patch: sampled error + legacy feature heuristic",
        "Adaptive distance hierarchy [CPU]",
        "Adaptive error hierarchy [CPU]",
        "Adaptive error + features [CPU]",
        "GPU tessellation: distance baseline",
        "GPU tessellation: projected max residual",
        "GPU tessellation: legacy feature heuristic",
        "GPU tessellation: RMS reconstruction-error baseline",
        "GPU tessellation: projected error + normal-angle threshold",
        "GPU tessellation: projected error + radiometric-sensitivity constraint"
    };
    int method_index = static_cast<int>(method_);
    if (ImGui::Combo("Method", &method_index, methods, 13)) {
        method_ = static_cast<ResearchMethod>(method_index);
        patch_set_.reset_history(experiment_config_.lod_count - 1);
        adaptive_mesh_.reset_history();
    }

    const char* scenes[] = {
        "Smooth hills [control]", "Sharp ridge [control]", "Mixed frequency [control]", "Cliff band [control]",
        "Eroded mountain", "Plateau canyon", "Alpine escarpment", "Glacial valley", "River basin",
        "Badlands", "Fractured highlands", "Mount St. Helens full mountain [USGS 3DEP 10 m]"
    };
    ImGui::TextDisabled("DEM source: validated canonical 10 m RAW");

    int scene_index = static_cast<int>(scene_);
    if (ImGui::Combo("Scene", &scene_index, scenes, surface_scene_count)) {
        const SurfaceScene previous_scene = scene_;
        const ResearchMethod previous_method = method_;
        scene_ = static_cast<SurfaceScene>(scene_index);
        if (scene_ == SurfaceScene::MountStHelensDem && !is_tessellation_method(method_)) {
            method_ = ResearchMethod::TessellationError;
        }
        if (scene_ == SurfaceScene::MountStHelensDem && method_ == ResearchMethod::TessellationContextAware) {
            method_ = ResearchMethod::TessellationError;
        }
        try {
            rebuild_scene();
            scene_load_error_.clear();
        } catch (const std::exception& e) {
            scene_load_error_ = e.what();
            scene_ = previous_scene;
            method_ = previous_method;
            try {
                rebuild_scene();
            } catch (...) {
            }
        }
    }
    ImGui::TextWrapped("%s", scene_description(scene_));
    if (!scene_load_error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.20f, 1.0f), "%s", scene_load_error_.c_str());
    }

    ImGui::SliderFloat(
        "Allowed projected error [px]",
        &experiment_config_.error_budget_px,
        0.1f,
        4.0f,
        "%.2f",
        ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Feature weight", &experiment_config_.feature_weight, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Allowed normal deviation [deg]", &experiment_config_.normal_budget_degrees, 0.5f, 20.0f, "%.2f");
    ImGui::SliderFloat(
        "Radiometric sensitivity threshold",
        &experiment_config_.radiance_budget,
        0.002f,
        0.15f,
        "%.4f",
        ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Scientific roughness", &experiment_config_.scientific_roughness, 0.08f, 1.0f, "%.2f");
    ImGui::SliderFloat("Light azimuth", &experiment_config_.light_azimuth_degrees, -180.0f, 180.0f, "%.0f deg");
    ImGui::SliderFloat("Light elevation", &experiment_config_.light_elevation_degrees, 5.0f, 85.0f, "%.0f deg");
    ImGui::SliderFloat("Hysteresis", &experiment_config_.hysteresis, 0.0f, 0.45f, "%.2f");
    ImGui::SliderFloat("Adaptive morph band", &experiment_config_.morph_band, 0.05f, 2.0f, "%.2f");
    ImGui::SliderInt("Distance LOD bias", &experiment_config_.distance_lod_bias, -2, 3);
    if (is_adaptive_method(method_)) {
        ImGui::Checkbox("Defer CPU LOD rebuild while moving", &defer_cpu_adaptation_while_moving_);
        if (defer_cpu_adaptation_while_moving_ && camera_idle_seconds_ < 0.08f) {
            ImGui::TextDisabled("Using previous watertight mesh until camera settles");
        }
    }
    if (is_tessellation_method(method_)) {
        ImGui::SliderInt("GPU patch grid", &experiment_config_.tessellation_patch_grid, 8, 64);
        ImGui::SliderFloat("Max tess factor", &experiment_config_.tessellation_max_level, 2.0f, 64.0f, "%.0f");
        ImGui::TextDisabled("fractional_even_spacing; tessellation is generated entirely on the GPU");
    }
    const char* render_modes[] = {
        "Scientific", "Realistic PBR terrain", "Tessellation-class map", "Projected-error ratio map",
        "Normal-direction variation", "Distance to error threshold", "Slope map"
    };
    int render_mode_index = static_cast<int>(experiment_config_.render_mode);
    if (ImGui::Combo("View", &render_mode_index, render_modes, 7)) {
        experiment_config_.render_mode = static_cast<RenderMode>(render_mode_index);
    }
    const char* shading_normal_modes[] = {"Auto", "Geometric (faceted)", "Smooth interpolated"};
    int shading_normal_mode_index = static_cast<int>(experiment_config_.shading_normal_mode);
    if (ImGui::Combo("Shading normals", &shading_normal_mode_index, shading_normal_modes, 3)) {
        experiment_config_.shading_normal_mode = static_cast<ShadingNormalMode>(shading_normal_mode_index);
    }
    ImGui::Checkbox("Wireframe", &experiment_config_.wireframe);
    ImGui::Text("Terrain materials: %s", terrain_textures_.has_external_assets() ? "loaded" : "fallback");

    ImGui::Separator();
    ImGui::TextUnformatted("Camera");
    const char* preset_names[camera_presets.size()]{};
    for (std::size_t i = 0; i < camera_presets.size(); ++i) preset_names[i] = camera_presets[i].name;
    if (ImGui::Combo(
            "Preset",
            &camera_preset_,
            preset_names,
            static_cast<int>(camera_presets.size()))) {
        apply_camera_preset(camera_preset_);
    }
    ImGui::SliderFloat("Camera distance", &camera_.distance(), 1.0f, 65.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderAngle("Camera yaw", &camera_.yaw(), -180.0f, 180.0f);
    ImGui::SliderAngle("Camera pitch", &camera_.pitch(), -3.0f, 88.0f);
    ImGui::SliderFloat("Camera FOV", &camera_.fov(), 32.0f, 82.0f, "%.1f deg");
    ImGui::SliderFloat("Move speed", &camera_speed_multiplier_, 0.15f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
    ImGui::TextWrapped(
        "RMB orbit | MMB pan | wheel or Q/E zoom | WASD move target | R/F vertical | "
        "arrows orbit | Shift faster | Home reset");
    if (ImGui::Button("Reset camera")) reset_camera_to_scene();
    ImGui::SameLine();
    if (ImGui::Button("Previous view")) {
        camera_preset_ = (camera_preset_ + static_cast<int>(camera_presets.size()) - 1) %
            static_cast<int>(camera_presets.size());
        apply_camera_preset(camera_preset_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Next view")) {
        camera_preset_ = (camera_preset_ + 1) % static_cast<int>(camera_presets.size());
        apply_camera_preset(camera_preset_);
    }

    if (ImGui::Button("Reload shaders")) {
        surface_shader_.reload();
        tessellation_shader_.reload();
        present_shader_.reload();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset LOD history")) {
        patch_set_.reset_history(experiment_config_.lod_count - 1);
        adaptive_mesh_.reset_history();
    }
    ImGui::SameLine();
    if (ImGui::Button("Screenshot")) {
        std::filesystem::create_directories("results");
        method_fbo_.bind();
        gfx::research::save_framebuffer_png("results/terrain.png", frame.width, frame.height);
    }

    ImGui::Separator();
    const int displayed_levels = is_adaptive_method(method_)
        ? std::min(experiment_config_.adaptive_max_level + 1, max_lod_count)
        : experiment_config_.lod_count;
    for (int lod = 0; lod < displayed_levels; ++lod) {
        ImGui::Text("Level %d regions: %zu", lod, last_render_stats_.lod_histogram[static_cast<std::size_t>(lod)]);
    }
    ImGui::End();
}

void TerrainResearchApp::on_resize(int width, int height) {
    if (benchmark_.active()) return;
    ensure_framebuffers(width, height);
}

void TerrainResearchApp::write_scene_rebuild_stage(const char* stage) const {
    try {
        const std::filesystem::path results = std::filesystem::path(GFX_TERRAIN_SOURCE_DIR) / "results";
        std::filesystem::create_directories(results);
        std::ofstream out(results / "last_scene_rebuild.txt", std::ios::trunc);
        out << "scene=" << scene_name(scene_) << '\n';
        out << "stage=" << stage << '\n';
        out.flush();
    } catch (...) {
    }
}

void TerrainResearchApp::write_dem_runtime_stage(const char* stage, const int patch_grid) const {
    if (scene_ != SurfaceScene::MountStHelensDem) return;
    std::cerr << "[DEM] " << stage;
    if (patch_grid >= 0) std::cerr << " | patch_grid=" << patch_grid;
    std::cerr << '\n' << std::flush;
    try {
        const std::filesystem::path results = std::filesystem::path(GFX_TERRAIN_SOURCE_DIR) / "results";
        std::filesystem::create_directories(results);
        std::ofstream out(results / "last_dem_runtime.txt", std::ios::trunc);
        out << "scene=" << scene_name(scene_) << '\n';
        out << "stage=" << stage << '\n';
        out << "method=" << method_name(method_) << '\n';
        out << "render_mode=" << render_mode_name(experiment_config_.render_mode) << '\n';
        out << "grid_size=" << heightfield_.grid_size() << '\n';
        if (patch_grid >= 0) out << "patch_grid=" << patch_grid << '\n';
        out.flush();
    } catch (...) {
    }
}

int TerrainResearchApp::active_tessellation_patch_grid() const {
    if (scene_ != SurfaceScene::MountStHelensDem) return experiment_config_.tessellation_patch_grid;

    // Keep interactive scene switching comfortably below the dense-reference workload.
    // The full 120 x 120 base grid is retained for automated DEM measurements, where
    // 120 patches x tessellation 16 reproduces the 1920 intervals of the source grid.
    return benchmark_.active() ? 120 : 48;
}

void TerrainResearchApp::ensure_cpu_scene_structures() {
    if (cpu_scene_structures_ready_) return;
    write_scene_rebuild_stage("building CPU patch/reference structures");
    if (!patch_set_.build(experiment_config_, heightfield_)) {
        throw std::runtime_error("Failed to build CPU patch set for scene " + std::string(scene_name(scene_)));
    }
    if (!adaptive_mesh_.build(
            heightfield_, experiment_config_.world_size, experiment_config_.adaptive_max_level, 4)) {
        throw std::runtime_error("Failed to build adaptive CPU mesh for scene " + std::string(scene_name(scene_)));
    }
    cpu_scene_structures_ready_ = true;
    write_scene_rebuild_stage("CPU structures ready");
}

void TerrainResearchApp::rebuild_scene() {
    // Drain any in-flight primitive query objects before changing the tessellation workload.
    tessellated_terrain_.reset_measurement();
    write_scene_rebuild_stage("loading heightfield");
    heightfield_.set_scene(scene_, experiment_config_.world_size);
    cpu_scene_structures_ready_ = false;
    dem_first_draw_verified_ = false;

    // Real-DEM validation is deliberately GPU-tessellation-only.  Building the legacy CPU
    // patch hierarchy here is unnecessary and made interactive scene selection execute a very
    // large amount of CPU preprocessing before the first frame.  It is now built lazily only
    // if the user explicitly selects a legacy CPU method.
    if (scene_ != SurfaceScene::MountStHelensDem) {
        ensure_cpu_scene_structures();
    }

    write_scene_rebuild_stage("uploading height/validity textures");
    upload_heightfield_texture();
    write_scene_rebuild_stage("applying camera preset");
    apply_camera_preset(camera_preset_);
    write_scene_rebuild_stage("complete");
}

void TerrainResearchApp::update_camera_controls(const gfx::research::FrameInfo& frame) {
    GLFWwindow* app_window = window();
    if (app_window == nullptr) return;

    double cursor_x = 0.0;
    double cursor_y = 0.0;
    glfwGetCursorPos(app_window, &cursor_x, &cursor_y);
    if (!camera_cursor_initialized_) {
        previous_cursor_x_ = cursor_x;
        previous_cursor_y_ = cursor_y;
        camera_cursor_initialized_ = true;
    }

    const double delta_x = cursor_x - previous_cursor_x_;
    const double delta_y = cursor_y - previous_cursor_y_;
    previous_cursor_x_ = cursor_x;
    previous_cursor_y_ = cursor_y;

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
        if (glfwGetMouseButton(app_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            camera_.yaw() -= static_cast<float>(delta_x) * 0.0045f;
            camera_.pitch() -= static_cast<float>(delta_y) * 0.0045f;
        }

        if (io.MouseWheel != 0.0f) {
            const float wheelScale = std::exp(-io.MouseWheel * 0.14f * camera_speed_multiplier_);
            camera_.distance() *= wheelScale;
        }

        if (glfwGetMouseButton(app_window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
            const glm::vec3 target = camera_.target();
            const glm::vec3 forward = glm::normalize(target - camera_.position());
            glm::vec3 right = glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f});
            if (glm::dot(right, right) < 1.0e-6f) right = {1.0f, 0.0f, 0.0f};
            else right = glm::normalize(right);
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));
            const float scale = std::max(camera_.distance(), 1.0f) * 0.0017f * camera_speed_multiplier_;
            camera_.target() += right * static_cast<float>(-delta_x) * scale;
            camera_.target() += up * static_cast<float>(delta_y) * scale;
        }
    }

    if (!io.WantCaptureKeyboard) {
        const bool fast = glfwGetKey(app_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
            || glfwGetKey(app_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        const float speed = camera_speed_multiplier_ * (fast ? 3.5f : 1.0f);
        const float move = std::max(camera_.distance() * 0.45f, 2.0f) * frame.delta_seconds * speed;
        const float yaw = camera_.yaw();
        const glm::vec3 forward{-std::sin(yaw), 0.0f, -std::cos(yaw)};
        const glm::vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};

        if (glfwGetKey(app_window, GLFW_KEY_W) == GLFW_PRESS) camera_.target() += forward * move;
        if (glfwGetKey(app_window, GLFW_KEY_S) == GLFW_PRESS) camera_.target() -= forward * move;
        if (glfwGetKey(app_window, GLFW_KEY_A) == GLFW_PRESS) camera_.target() -= right * move;
        if (glfwGetKey(app_window, GLFW_KEY_D) == GLFW_PRESS) camera_.target() += right * move;
        if (glfwGetKey(app_window, GLFW_KEY_R) == GLFW_PRESS) camera_.target().y += move;
        if (glfwGetKey(app_window, GLFW_KEY_F) == GLFW_PRESS) camera_.target().y -= move;

        const float zoom_rate = 1.65f * frame.delta_seconds * speed;
        if (glfwGetKey(app_window, GLFW_KEY_Q) == GLFW_PRESS) camera_.distance() *= std::exp(-zoom_rate);
        if (glfwGetKey(app_window, GLFW_KEY_E) == GLFW_PRESS) camera_.distance() *= std::exp(zoom_rate);

        const float orbit_rate = 1.15f * frame.delta_seconds * speed;
        if (glfwGetKey(app_window, GLFW_KEY_LEFT) == GLFW_PRESS) camera_.yaw() -= orbit_rate;
        if (glfwGetKey(app_window, GLFW_KEY_RIGHT) == GLFW_PRESS) camera_.yaw() += orbit_rate;
        if (glfwGetKey(app_window, GLFW_KEY_UP) == GLFW_PRESS) camera_.pitch() += orbit_rate;
        if (glfwGetKey(app_window, GLFW_KEY_DOWN) == GLFW_PRESS) camera_.pitch() -= orbit_rate;
        if (glfwGetKey(app_window, GLFW_KEY_HOME) == GLFW_PRESS) reset_camera_to_scene();
    }

    camera_.distance() = std::clamp(camera_.distance(), 0.8f, 80.0f);
    camera_.pitch() = std::clamp(camera_.pitch(), -0.08f, 1.535f);
    camera_.fov() = std::clamp(camera_.fov(), 32.0f, 82.0f);
    const float half = experiment_config_.world_size * 0.72f;
    camera_.target().x = std::clamp(camera_.target().x, -half, half);
    camera_.target().z = std::clamp(camera_.target().z, -half, half);
    const float vertical_margin = std::max(heightfield_.max_height() - heightfield_.min_height(), 1.0f);
    camera_.target().y = std::clamp(
        camera_.target().y,
        heightfield_.min_height() - vertical_margin,
        heightfield_.max_height() + vertical_margin);
}

void TerrainResearchApp::apply_camera_preset(int preset_index) {
    preset_index = std::clamp(preset_index, 0, static_cast<int>(camera_presets.size()) - 1);
    camera_preset_ = preset_index;
    const CameraPreset& preset = camera_presets[static_cast<std::size_t>(preset_index)];

    float target_x = preset.target_x;
    float target_z = preset.target_z;
    float target_height_offset = preset.target_height_offset;
    float distance_scale = preset.distance_scale;
    float yaw = preset.yaw;
    float pitch = preset.pitch;

    if (scene_ == SurfaceScene::MountStHelensDem && preset_index == 0) {
        // The summit-centered USGS 3DEP crop is regular and continuous, so no
        // survey-footprint offset is necessary.
        target_x = 0.0f;
        target_z = 0.0f;
        target_height_offset = 0.10f;
        distance_scale = 1.08f;
        yaw = 2.34f;
        pitch = 0.48f;
    }

    const float x = target_x * experiment_config_.world_size;
    const float z = target_z * experiment_config_.world_size;
    const float range = std::max(heightfield_.max_height() - heightfield_.min_height(), 0.5f);
    const float surface_y = heightfield_.height(x, z);
    camera_.set_target({x, surface_y + target_height_offset * range, z});
    camera_.set_distance(std::max(1.0f, distance_scale * experiment_config_.world_size));
    camera_.set_yaw(yaw);
    camera_.set_pitch(pitch);
}

void TerrainResearchApp::reset_camera_to_scene() {
    camera_preset_ = 0;
    apply_camera_preset(camera_preset_);
}

int TerrainResearchApp::run_python_tool(
    const std::filesystem::path& script,
    const std::filesystem::path* argument) const {

    const std::filesystem::path python = GFX_TERRAIN_PYTHON_EXECUTABLE;
#if defined(_WIN32)
    const std::wstring executable = python.wstring();
    const std::wstring script_path = script.wstring();
    std::wstring argument_path;
    std::vector<const wchar_t*> arguments;
    arguments.reserve(argument == nullptr ? 3 : 4);
    arguments.push_back(executable.c_str());
    arguments.push_back(script_path.c_str());
    if (argument != nullptr) {
        argument_path = argument->wstring();
        arguments.push_back(argument_path.c_str());
    }
    arguments.push_back(nullptr);

    const intptr_t result = _wspawnvp(_P_WAIT, executable.c_str(), arguments.data());
    return result == -1 ? -1 : static_cast<int>(result);
#else
    const std::string executable = python.string();
    const std::string script_path = script.string();
    const std::string argument_path = argument == nullptr ? std::string{} : argument->string();
    const pid_t child = fork();
    if (child == -1) return -1;
    if (child == 0) {
        if (argument == nullptr) {
            execlp(executable.c_str(), executable.c_str(), script_path.c_str(), nullptr);
        } else {
            execlp(
                executable.c_str(),
                executable.c_str(),
                script_path.c_str(),
                argument_path.c_str(),
                nullptr);
        }
        _exit(127);
    }

    int status = 0;
    if (waitpid(child, &status, 0) == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

void TerrainResearchApp::start_gui_test_suite(
    const GuiTestSuite suite,
    const gfx::research::FrameInfo& frame) {

    if (benchmark_.active() || suite == GuiTestSuite::None) return;

    gui_restore_config_ = experiment_config_;
    gui_restore_method_ = method_;
    gui_restore_scene_ = scene_;
    gui_restore_camera_preset_ = camera_preset_;
    const glm::vec3 target = camera_.target();
    gui_restore_camera_target_ = {target.x, target.y, target.z};
    gui_restore_camera_distance_ = camera_.distance();
    gui_restore_camera_yaw_ = camera_.yaw();
    gui_restore_camera_pitch_ = camera_.pitch();
    gui_restore_camera_fov_ = camera_.fov();

    gui_test_suite_ = suite;
    gui_test_status_ = GuiTestStatus::Running;
    gui_test_message_ = "Running unit/static tests...";
    if (suite == GuiTestSuite::Validation) gui_validation_passed_ = false;

    const std::filesystem::path root = GFX_TERRAIN_SOURCE_DIR;
    if (suite == GuiTestSuite::RealDemValidation) {
        const std::filesystem::path dem_asset = root
            / "assets" / "dem" / "mount_st_helens_usgs_10m_1921_f32.raw";
        constexpr std::uintmax_t expected_dem_bytes = 1921ull * 1921ull * sizeof(float);
        std::error_code dem_error;
        const bool raw_ready = std::filesystem::is_regular_file(dem_asset, dem_error)
            && !dem_error
            && std::filesystem::file_size(dem_asset, dem_error) == expected_dem_bytes
            && !dem_error;
        if (!raw_ready) {
            gui_test_status_ = GuiTestStatus::Failed;
            gui_test_message_ = "Mount St. Helens DEM RAW asset is missing or has an invalid size.";
            restore_interactive_state();
            return;
        }
    }
    const int unit_result = run_python_tool(root / "tools/run_unit_tests.py", nullptr);
    if (unit_result != 0) {
        gui_test_status_ = GuiTestStatus::Failed;
        gui_test_message_ = "Unit/static tests failed. GPU suite was not started.";
        restore_interactive_state();
        return;
    }

    ExperimentConfig suite_config = experiment_config_;
    suite_config.validation_suite = suite == GuiTestSuite::Validation;
    suite_config.publication_quick = suite == GuiTestSuite::DoctoralQuick;
    suite_config.publication_suite = suite == GuiTestSuite::DoctoralFull;
    suite_config.thesis_figure_suite = false;
    suite_config.thesis_pbr_figure_suite = suite == GuiTestSuite::PbrFigures;
    suite_config.real_dem_validation_suite = suite == GuiTestSuite::RealDemValidation;
    suite_config.render_mode = suite == GuiTestSuite::PbrFigures ? RenderMode::Realistic : RenderMode::Scientific;
    suite_config.shading_normal_mode = suite == GuiTestSuite::PbrFigures
        ? ShadingNormalMode::Smooth
        : ShadingNormalMode::Auto;
    suite_config.wireframe = false;
    const char* output_name = "doctoral-full-gui";
    if (suite == GuiTestSuite::Validation) output_name = "validation-gui";
    else if (suite == GuiTestSuite::DoctoralQuick) output_name = "doctoral-quick-gui";
    else if (suite == GuiTestSuite::PbrFigures) output_name = "thesis-pbr-gui";
    else if (suite == GuiTestSuite::RealDemValidation) output_name = "real-dem-validation-gui";
    suite_config.output_dir = root / "results" / output_name;

    std::error_code error;
    std::filesystem::remove_all(suite_config.output_dir, error);
    gui_test_output_dir_ = suite_config.output_dir;
    experiment_config_ = suite_config;
    benchmark_.configure(suite_config);
    benchmark_.begin(runtime_info(frame.width, frame.height));
    if (!benchmark_.active()) {
        gui_test_status_ = GuiTestStatus::Failed;
        gui_test_message_ = "Benchmark suite contains no cases.";
        restore_interactive_state();
        return;
    }

    benchmark_started_ = true;
    applied_benchmark_case_ = static_cast<std::size_t>(-1);
    if (suite == GuiTestSuite::Validation) {
        gui_test_message_ = "Running GPU validation and topology/null checks...";
    } else if (suite == GuiTestSuite::DoctoralQuick) {
        gui_test_message_ = "Running expanded doctoral quick benchmark...";
    } else if (suite == GuiTestSuite::PbrFigures) {
        gui_test_message_ = "Rendering full PBR thesis screenshots with smooth normals...";
    } else if (suite == GuiTestSuite::RealDemValidation) {
        gui_test_message_ = "Running external validation on the measured Mount St. Helens DEM...";
    } else {
        gui_test_message_ = "Running full doctoral benchmark...";
    }
}

void TerrainResearchApp::finish_gui_test_suite(
    const gfx::research::FrameInfo& frame,
    const bool cancelled) {

    if (cancelled && benchmark_.active()) benchmark_.finalize();

    const GuiTestSuite completed_suite = gui_test_suite_;
    if (cancelled) {
        gui_test_status_ = GuiTestStatus::Cancelled;
        gui_test_message_ = "Test run cancelled by user.";
    } else {
        const std::filesystem::path root = GFX_TERRAIN_SOURCE_DIR;
        const bool pbr_suite = completed_suite == GuiTestSuite::PbrFigures;
        const bool real_dem_suite = completed_suite == GuiTestSuite::RealDemValidation;
        gui_test_message_ = pbr_suite ? "Finalizing PBR screenshot captures..." : "Analyzing generated results...";
        const int analysis_result = pbr_suite
            ? 0
            : run_python_tool(
                root / (real_dem_suite ? "tools/analyze_real_dem.py" : "tools/analyze_results.py"),
                &gui_test_output_dir_);
        int validation_result = 0;
        if (completed_suite == GuiTestSuite::Validation && analysis_result == 0) {
            validation_result = run_python_tool(root / "tools/validate_results.py", &gui_test_output_dir_);
        }

        if (analysis_result == 0 && validation_result == 0) {
            gui_test_status_ = GuiTestStatus::Passed;
            if (completed_suite == GuiTestSuite::Validation) {
                gui_validation_passed_ = true;
                gui_test_message_ = "Validation passed.";
            } else if (completed_suite == GuiTestSuite::DoctoralQuick) {
                gui_test_message_ = "Doctoral quick completed and analysis files were generated.";
            } else if (completed_suite == GuiTestSuite::PbrFigures) {
                gui_test_message_ = "PBR screenshot tests completed. Raw captures were generated in the results directory.";
            } else if (completed_suite == GuiTestSuite::RealDemValidation) {
                gui_test_message_ = "Real DEM validation completed. Summary and raw captures were generated.";
            } else {
                gui_test_message_ = "Doctoral full completed and analysis files were generated.";
            }
        } else {
            gui_test_status_ = GuiTestStatus::Failed;
            if (completed_suite == GuiTestSuite::Validation) gui_validation_passed_ = false;
            gui_test_message_ = analysis_result != 0
                ? "Result analysis failed. See the console output."
                : "Validation gates failed. See validation_report.md.";
        }
    }

    benchmark_started_ = false;
    applied_benchmark_case_ = static_cast<std::size_t>(-1);
    restore_interactive_state();
    ensure_framebuffers(frame.width, frame.height);
}

void TerrainResearchApp::restore_interactive_state() {
    experiment_config_ = gui_restore_config_;
    method_ = gui_restore_method_;
    scene_ = gui_restore_scene_;
    camera_preset_ = gui_restore_camera_preset_;
    rebuild_scene();
    camera_.set_target({
        gui_restore_camera_target_[0],
        gui_restore_camera_target_[1],
        gui_restore_camera_target_[2]});
    camera_.set_distance(gui_restore_camera_distance_);
    camera_.set_yaw(gui_restore_camera_yaw_);
    camera_.set_pitch(gui_restore_camera_pitch_);
    camera_.set_fov(gui_restore_camera_fov_);
    camera_idle_seconds_ = 1.0f;
    camera_moved_this_frame_ = false;
}

void TerrainResearchApp::upload_heightfield_texture() {
    write_dem_runtime_stage("texture upload begin");
    if (scene_ == SurfaceScene::MountStHelensDem) {
        constexpr std::size_t expected_samples = 1921ull * 1921ull;
        if (heightfield_.grid_size() != 1921 || heightfield_.grid_data().size() != expected_samples) {
            throw std::runtime_error("DEM CPU grid contract failed before OpenGL upload.");
        }
        while (glGetError() != GL_NO_ERROR) {}
        int max_texture_size = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
        if (heightfield_.grid_size() > max_texture_size) {
            throw std::runtime_error("DEM working texture exceeds GL_MAX_TEXTURE_SIZE");
        }
    }
    if (heightfield_texture_ != 0) {
        glDeleteTextures(1, &heightfield_texture_);
        heightfield_texture_ = 0;
    }
    if (heightfield_validity_texture_ != 0) {
        glDeleteTextures(1, &heightfield_validity_texture_);
        heightfield_validity_texture_ = 0;
    }

    write_dem_runtime_stage("creating height texture");
    glCreateTextures(GL_TEXTURE_2D, 1, &heightfield_texture_);
    write_dem_runtime_stage("creating validity texture");
    glCreateTextures(GL_TEXTURE_2D, 1, &heightfield_validity_texture_);

    int texture_size = 513;
    std::vector<unsigned char> validity;
    if (heightfield_.has_grid()) {
        texture_size = heightfield_.grid_size();
        write_dem_runtime_stage("allocating height texture storage");
        glTextureStorage2D(heightfield_texture_, 1, GL_R32F, texture_size, texture_size);
        write_dem_runtime_stage("uploading height texture samples");
        glTextureSubImage2D(
            heightfield_texture_, 0, 0, 0, texture_size, texture_size,
            GL_RED, GL_FLOAT, heightfield_.grid_data().data());
        write_dem_runtime_stage("height texture samples uploaded");
        if (heightfield_.has_validity_mask()) validity = heightfield_.validity_data();
    } else {
        // Tessellation requires a sampled heightfield even for analytic control scenes.
        constexpr int size = 513;
        texture_size = size;
        std::vector<float> sampled(static_cast<std::size_t>(size * size));
        const float half = experiment_config_.world_size * 0.5f;
        for (int z = 0; z < size; ++z) {
            const float world_z = std::lerp(-half, half, static_cast<float>(z) / static_cast<float>(size - 1));
            for (int x = 0; x < size; ++x) {
                const float world_x = std::lerp(-half, half, static_cast<float>(x) / static_cast<float>(size - 1));
                sampled[static_cast<std::size_t>(z * size + x)] = heightfield_.height(world_x, world_z);
            }
        }
        glTextureStorage2D(heightfield_texture_, 1, GL_R32F, size, size);
        glTextureSubImage2D(
            heightfield_texture_, 0, 0, 0, size, size,
            GL_RED, GL_FLOAT, sampled.data());
        validity.assign(static_cast<std::size_t>(size * size), 255u);
    }

    glTextureParameteri(heightfield_texture_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(heightfield_texture_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(heightfield_texture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(heightfield_texture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (heightfield_.has_validity_mask()) {
        write_dem_runtime_stage("allocating validity texture storage");
        glTextureStorage2D(heightfield_validity_texture_, 1, GL_R8, texture_size, texture_size);
        glTextureSubImage2D(
            heightfield_validity_texture_, 0, 0, 0, texture_size, texture_size,
            GL_RED, GL_UNSIGNED_BYTE, validity.data());
    } else {
        // A continuous DEM does not need a second full-resolution 1921 x 1921 texture.
        // Keep a valid 1 x 1 white object bound for the common shader interface; the
        // shader does not sample it when uUseValidityMask is false.
        constexpr unsigned char valid = 255u;
        write_dem_runtime_stage("creating compact validity fallback");
        glTextureStorage2D(heightfield_validity_texture_, 1, GL_R8, 1, 1);
        glTextureSubImage2D(
            heightfield_validity_texture_, 0, 0, 0, 1, 1,
            GL_RED, GL_UNSIGNED_BYTE, &valid);
    }
    glTextureParameteri(heightfield_validity_texture_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(heightfield_validity_texture_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(heightfield_validity_texture_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(heightfield_validity_texture_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (scene_ == SurfaceScene::MountStHelensDem) {
        glFinish();
        const unsigned int error = glGetError();
        if (error != GL_NO_ERROR) {
            throw std::runtime_error("OpenGL error while uploading DEM textures: " + std::to_string(error));
        }
        write_dem_runtime_stage("texture upload complete");
    }
}

void TerrainResearchApp::ensure_framebuffers(int width, int height) {
    if (width <= 0 || height <= 0) return;
    const std::array<gfx::research::ColorAttachmentDesc, 1> colors{{{GL_RGBA16F, GL_LINEAR, GL_LINEAR}}};
    if (!framebuffers_ready_) {
        method_fbo_.create(width, height, colors, true, GL_DEPTH_COMPONENT32F);
        reference_fbo_.create(width, height, colors, true, GL_DEPTH_COMPONENT32F);
        difference_fbo_.create(width, height, colors, false);
        framebuffers_ready_ = true;
        return;
    }
    if (method_fbo_.width() != width || method_fbo_.height() != height) {
        method_fbo_.resize(width, height);
        reference_fbo_.resize(width, height);
        difference_fbo_.resize(width, height);
    }
}

void TerrainResearchApp::apply_benchmark_case(const BenchmarkCase& test_case) {
    const bool scene_changed = scene_ != test_case.scene;
    scene_ = test_case.scene;
    method_ = test_case.method;
    experiment_config_.error_budget_px = test_case.error_budget_px;
    experiment_config_.normal_budget_degrees = test_case.normal_budget_degrees;
    experiment_config_.radiance_budget = test_case.radiance_budget;
    experiment_config_.scientific_roughness = test_case.scientific_roughness;
    experiment_config_.light_azimuth_degrees = test_case.light_azimuth_degrees;
    experiment_config_.light_elevation_degrees = test_case.light_elevation_degrees;
    experiment_config_.distance_lod_bias = test_case.distance_lod_bias;
    experiment_config_.hysteresis = test_case.hysteresis;
    experiment_config_.morph_band = test_case.morph_band;
    experiment_config_.render_mode = test_case.benchmark_render_mode;
    experiment_config_.shading_normal_mode = test_case.shading_normal_mode;
    experiment_config_.wireframe = false;

    if (scene_changed) rebuild_scene();
    else if (cpu_scene_structures_ready_) {
        patch_set_.reset_history(experiment_config_.lod_count - 1);
        adaptive_mesh_.reset_history();
    }
    tessellated_terrain_.reset_measurement();
    write_lod_error_table(scene_);

    const float target_x = test_case.camera_target_x * experiment_config_.world_size;
    const float target_z = test_case.camera_target_z * experiment_config_.world_size;
    const float height_range = std::max(heightfield_.max_height() - heightfield_.min_height(), 0.5f);
    const float target_y = heightfield_.height(target_x, target_z)
        + test_case.camera_target_height_offset * height_range;
    camera_.set_target({target_x, target_y, target_z});
    camera_.set_distance(test_case.camera_distance);
    camera_.set_yaw(test_case.camera_yaw);
    camera_.set_pitch(test_case.camera_pitch);
    camera_.set_fov(test_case.camera_fov_degrees);
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
        if ((method == ResearchMethod::ErrorBounded || method == ResearchMethod::ErrorBoundedFeatureAware)
            && lod == experiment_config_.lod_count - 1
            && controlled > static_cast<double>(context.error_budget_px)) {
            ++stats.saturated_regions;
        }
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
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, experiment_config_.wireframe ? GL_LINE : GL_FILL);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    if (scene_ == SurfaceScene::MountStHelensDem) {
        constexpr std::size_t expected_samples = 1921ull * 1921ull;
        if (heightfield_.grid_size() != 1921 || heightfield_.grid_data().size() != expected_samples
            || heightfield_texture_ == 0 || heightfield_validity_texture_ == 0) {
            throw std::runtime_error("DEM render contract failed before draw submission.");
        }
    }
    const bool dem_dense_reference = scene_ == SurfaceScene::MountStHelensDem
        && method == ResearchMethod::Reference;
    const bool use_tessellation_path = is_tessellation_method(method) || dem_dense_reference;
    gfx::research::Shader& active_shader = use_tessellation_path
        ? tessellation_shader_
        : surface_shader_;
    active_shader.bind();
    active_shader.set_mat4("uMvp", camera_.view_projection(aspect));
    active_shader.set_vec3("uEye", camera_.position());
    const glm::vec3 light_dir = light_direction(
        experiment_config_.light_azimuth_degrees,
        experiment_config_.light_elevation_degrees);
    active_shader.set_vec3("uLightDir", light_dir);
    active_shader.set_float("uScientificRoughness", experiment_config_.scientific_roughness);
    active_shader.set_int("uShadingNormalMode", static_cast<int>(experiment_config_.shading_normal_mode));
    const RenderMode active_render_mode = benchmark_.active()
        ? (capture_render_mode_override_enabled_ ? capture_render_mode_ : experiment_config_.render_mode)
        : experiment_config_.render_mode;
    active_shader.set_int("uRenderMode", static_cast<int>(active_render_mode));
    active_shader.set_int("uScene", static_cast<int>(scene_));
    active_shader.set_bool("uUseHeightMap", heightfield_.has_grid());
    active_shader.set_float("uWorldSize", experiment_config_.world_size);
    active_shader.set_float("uHeightMin", heightfield_.min_height());
    active_shader.set_float("uHeightMax", heightfield_.max_height());
    active_shader.set_int("uGrassDiffuse", 0);
    active_shader.set_int("uGrassNormal", 1);
    active_shader.set_int("uGrassRoughness", 2);
    active_shader.set_int("uGrassHeight", 3);
    active_shader.set_int("uDirtDiffuse", 4);
    active_shader.set_int("uDirtNormal", 5);
    active_shader.set_int("uDirtRoughness", 6);
    active_shader.set_int("uDirtHeight", 7);
    active_shader.set_int("uRockDiffuse", 8);
    active_shader.set_int("uRockNormal", 9);
    active_shader.set_int("uRockRoughness", 10);
    active_shader.set_int("uRockHeight", 11);
    active_shader.set_int("uSandDiffuse", 12);
    active_shader.set_int("uSandNormal", 13);
    active_shader.set_int("uSandRoughness", 14);
    active_shader.set_int("uSandHeight", 15);
    active_shader.set_int("uHeightMap", 16);
    active_shader.set_int("uValidityMap", 17);
    active_shader.set_bool("uUseValidityMask", heightfield_.has_validity_mask());
    terrain_textures_.bind();
    glBindTextureUnit(16, heightfield_texture_);
    glBindTextureUnit(17, heightfield_validity_texture_);

    SelectionContext context;
    context.camera_position = camera_.position();
    context.viewport_height = height;
    context.vertical_fov_degrees = camera_.fov();
    context.error_budget_px = experiment_config_.error_budget_px;
    context.feature_weight = experiment_config_.feature_weight;
    context.hysteresis = experiment_config_.hysteresis;
    context.distance_lod_bias = experiment_config_.distance_lod_bias;

    RenderStats stats;
    if (use_tessellation_path) {
        const int patch_grid = active_tessellation_patch_grid();
        active_shader.set_int("uTessPatchGrid", patch_grid);
        active_shader.set_int("uViewportWidth", width);
        active_shader.set_int("uViewportHeight", height);
        active_shader.set_float("uVerticalFovDegrees", camera_.fov());
        active_shader.set_float("uErrorBudgetPx", experiment_config_.error_budget_px);
        active_shader.set_float("uFeatureWeight", experiment_config_.feature_weight);
        active_shader.set_float("uNormalBudgetDegrees", experiment_config_.normal_budget_degrees);
        active_shader.set_float("uRadianceBudget", experiment_config_.radiance_budget);
        active_shader.set_int("uDistanceLodBias", experiment_config_.distance_lod_bias);
        active_shader.set_float("uMaxTessLevel", experiment_config_.tessellation_max_level);
        active_shader.set_bool("uForceMaxTessellation", force_finest || dem_dense_reference);
        int tess_method = 0;
        switch (method) {
        case ResearchMethod::TessellationDistance: tess_method = 0; break;
        case ResearchMethod::TessellationError: tess_method = 1; break;
        case ResearchMethod::TessellationErrorFeatureAware: tess_method = 2; break;
        case ResearchMethod::TessellationVarianceBaseline: tess_method = 3; break;
        case ResearchMethod::TessellationNormalBound: tess_method = 4; break;
        case ResearchMethod::TessellationContextAware: tess_method = 5; break;
        default: break;
        }
        active_shader.set_int("uTessMethod", tess_method);
        const bool verify_dem_draw = scene_ == SurfaceScene::MountStHelensDem && !dem_first_draw_verified_;
        if (verify_dem_draw) {
            while (glGetError() != GL_NO_ERROR) {}
            write_dem_runtime_stage("before tessellation draw", patch_grid);
        }
        if (time_gpu) gpu_timer_.begin();
        const bool dem_scene = scene_ == SurfaceScene::MountStHelensDem;
        const bool synchronize_triangle_count = benchmark_.active() && benchmark_.frame_in_case() == 0;
        const bool measure_primitives = !dem_scene || synchronize_triangle_count;
        tessellated_terrain_.draw(patch_grid, synchronize_triangle_count, measure_primitives);
        write_dem_runtime_stage("tessellation draw submitted", patch_grid);
        if (time_gpu) gpu_timer_.end();
        if (verify_dem_draw) {
            glFinish();
            const unsigned int error = glGetError();
            if (error != GL_NO_ERROR) {
                throw std::runtime_error("OpenGL error during DEM tessellation draw: " + std::to_string(error));
            }
            dem_first_draw_verified_ = true;
            write_dem_runtime_stage("first tessellation draw complete", patch_grid);
        }
        stats.triangles = tessellated_terrain_.last_triangle_count();
        stats.adaptive_leaf_count = static_cast<std::size_t>(patch_grid * patch_grid);
        stats.adaptation_cpu_ms = 0.0;
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        return stats;
    }

    if (is_adaptive_method(method)) {
        ensure_cpu_scene_structures();
        const auto update_begin = std::chrono::steady_clock::now();
        const bool allow_rebuild = benchmark_.active()
            || !defer_cpu_adaptation_while_moving_
            || camera_idle_seconds_ >= 0.08f
            || !adaptive_mesh_.has_cached_geometry();
        adaptive_mesh_.update(
            heightfield_, method, context, experiment_config_.morph_band, force_finest, stats, allow_rebuild);
        const auto update_end = std::chrono::steady_clock::now();
        stats.adaptation_cpu_ms = std::chrono::duration<double, std::milli>(update_end - update_begin).count();
        if (experiment_config_.validation_suite && benchmark_.active() && benchmark_.should_capture_quality()) {
            const TopologyValidation topology = adaptive_mesh_.validate_topology();
            stats.topology_open_internal_edges = topology.open_internal_edges;
            stats.topology_nonmanifold_edges = topology.nonmanifold_edges;
        }
        surface_shader_.set_bool("uAdaptiveMesh", true);
        surface_shader_.set_int("uLod", 0);
        surface_shader_.set_float("uErrorRatio", 0.0f);
        if (time_gpu) gpu_timer_.begin();
        adaptive_mesh_.draw();
        if (time_gpu) gpu_timer_.end();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        return stats;
    }

    ensure_cpu_scene_structures();
    surface_shader_.set_bool("uAdaptiveMesh", false);
    const auto update_begin = std::chrono::steady_clock::now();
    const std::vector<int> selected_lods = select_lods(method, context, force_finest, stats);
    const auto update_end = std::chrono::steady_clock::now();
    stats.adaptation_cpu_ms = std::chrono::duration<double, std::milli>(update_end - update_begin).count();
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
        const bool feature_aware = method == ResearchMethod::ErrorBoundedFeatureAware;
        const double controlled_error = controller_.projected_error_px(patch, lod, context, feature_aware);
        const double safe_budget = std::max(static_cast<double>(context.error_budget_px), 1.0e-6);
        const float error_ratio = static_cast<float>(controlled_error / safe_budget);
        surface_shader_.set_float("uErrorRatio", error_ratio);
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
    present_shader_.set_int("uRenderMode", static_cast<int>(experiment_config_.render_mode));
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
        compare_silhouette(ref_depth, test_depth, test_case.width, test_case.height),
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

    // This image is produced directly from the two linear-HDR render targets used by
    // the quality measurement. It is therefore a test artifact, not a figure synthesized
    // afterwards from tone-mapped PNG files. The scale is fixed across thesis captures.
    save_rgb_error_capture(test_case, capture_dir, stem);

    if (!test_case.capture_diagnostics) return;

    const auto save_method_mode = [&](RenderMode mode, const char* suffix) {
        capture_render_mode_override_enabled_ = true;
        capture_render_mode_ = mode;
        render_surface(
            test_case.method,
            method_fbo_,
            test_case.width,
            test_case.height,
            false,
            test_case.force_finest);
        method_fbo_.bind();
        gfx::research::save_framebuffer_png(
            capture_dir / (stem + suffix),
            test_case.width,
            test_case.height);
    };

    save_method_mode(RenderMode::Realistic, "-realistic.png");
    save_method_mode(RenderMode::LodHeatmap, "-lod.png");
    save_method_mode(RenderMode::ErrorHeatmap, "-error.png");
    save_method_mode(RenderMode::NormalVariation, "-normal.png");
    save_method_mode(RenderMode::BudgetMargin, "-margin.png");
    save_method_mode(RenderMode::SlopeHeatmap, "-slope.png");

    capture_render_mode_ = RenderMode::Realistic;
    render_surface(
        ResearchMethod::Reference,
        reference_fbo_,
        test_case.width,
        test_case.height,
        false,
        true);
    reference_fbo_.bind();
    gfx::research::save_framebuffer_png(
        capture_dir / (stem + "-reference-realistic.png"),
        test_case.width,
        test_case.height);

    capture_render_mode_override_enabled_ = false;
    capture_render_mode_ = RenderMode::Scientific;
}

void TerrainResearchApp::save_rgb_error_capture(
    const BenchmarkCase& test_case,
    const std::filesystem::path& capture_dir,
    const std::string& stem) {

    difference_fbo_.bind();
    glViewport(0, 0, test_case.width, test_case.height);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    difference_shader_.bind();
    glBindTextureUnit(0, reference_fbo_.color());
    glBindTextureUnit(1, method_fbo_.color());
    difference_shader_.set_int("uReference", 0);
    difference_shader_.set_int("uCandidate", 1);
    difference_shader_.set_float("uErrorMax", 0.16f);
    fullscreen_triangle_.draw();

    difference_fbo_.bind();
    gfx::research::save_framebuffer_png(
        capture_dir / (stem + "-rgb-error.png"),
        test_case.width,
        test_case.height);
    glEnable(GL_DEPTH_TEST);
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
    case ResearchMethod::AdaptiveDistance: return "adaptive-distance";
    case ResearchMethod::AdaptiveError: return "adaptive-error";
    case ResearchMethod::AdaptiveErrorFeatureAware: return "adaptive-error-feature";
    case ResearchMethod::TessellationDistance: return "tessellation-distance";
    case ResearchMethod::TessellationError: return "tessellation-error";
    case ResearchMethod::TessellationErrorFeatureAware: return "tessellation-error-feature";
    case ResearchMethod::TessellationVarianceBaseline: return "tessellation-variance-baseline";
    case ResearchMethod::TessellationNormalBound: return "tessellation-normal-bound";
    case ResearchMethod::TessellationContextAware: return "tessellation-context-aware";
    }
    return "unknown";
}

const char* scene_name(SurfaceScene scene) {
    switch (scene) {
    case SurfaceScene::SmoothHills: return "smooth-hills";
    case SurfaceScene::SharpRidge: return "sharp-ridge";
    case SurfaceScene::MixedFrequency: return "mixed-frequency";
    case SurfaceScene::CliffBand: return "cliff-band";
    case SurfaceScene::ErodedMountain: return "eroded-mountain";
    case SurfaceScene::PlateauCanyon: return "plateau-canyon";
    case SurfaceScene::AlpineEscarpment: return "alpine-escarpment";
    case SurfaceScene::GlacialValley: return "glacial-valley";
    case SurfaceScene::RiverBasin: return "river-basin";
    case SurfaceScene::Badlands: return "badlands";
    case SurfaceScene::FracturedHighlands: return "fractured-highlands";
    case SurfaceScene::MountStHelensDem: return "mount-st-helens-dem";
    }
    return "unknown";
}

const char* scene_description(SurfaceScene scene) {
    switch (scene) {
    case SurfaceScene::SmoothHills:
        return "Analytic low-frequency control: tests whether the controller avoids unnecessary refinement "
            "on smooth terrain.";
    case SurfaceScene::SharpRidge:
        return "Analytic ridge control: isolates a narrow high-curvature feature and silhouette preservation.";
    case SurfaceScene::MixedFrequency:
        return "Analytic mixed-band control: combines low, medium and high spatial frequencies in one surface.";
    case SurfaceScene::CliffBand:
        return "Analytic steep-transition control: stresses projected error and feature protection around "
            "a cliff-like band.";
    case SurfaceScene::ErodedMountain:
        return "Naturalistic mountain massif with domain warping, drainage carving and thermal erosion; "
            "broad external-validity case.";
    case SurfaceScene::PlateauCanyon:
        return "Plateau, canyon and tributary system: combines flat cheap regions with localized expensive boundaries.";
    case SurfaceScene::AlpineEscarpment:
        return "Large escarpment, peaks, gullies and strata: stresses silhouettes, cliffs and "
            "heterogeneous local detail.";
    case SurfaceScene::GlacialValley:
        return "U-shaped glacial valley with smooth floor, steep walls, cirques and ridges; strong "
            "spatial heterogeneity.";
    case SurfaceScene::RiverBasin:
        return "Rolling drainage basin with main channel and tributaries; tests thin features embedded "
            "in smoother terrain.";
    case SurfaceScene::Badlands:
        return "Dense gullies, mesas and layered erosion; adversarial high-frequency terrain where "
            "refinement may saturate.";
    case SurfaceScene::FracturedHighlands:
        return "Faulted highlands with basins, ridges and shelves; tests multiple feature types and "
            "abrupt local changes.";
    case SurfaceScene::MountStHelensDem:
        return "USGS 3DEP 1/3 arc-second Mount St. Helens heightmap: "
            "1921 x 1921 samples at 10 m spacing, covering a 19.2 km square centered on the summit. "
            "Unlike the earlier DS904 lidar survey footprint, this source is spatially continuous and is rendered as one regular heightfield without a validity-mask cutout.";
    }
    return "Unknown scene.";
}

const char* render_mode_name(RenderMode mode) {
    switch (mode) {
    case RenderMode::Scientific: return "scientific";
    case RenderMode::Realistic: return "realistic";
    case RenderMode::LodHeatmap: return "lod-heatmap";
    case RenderMode::ErrorHeatmap: return "error-heatmap";
    case RenderMode::NormalVariation: return "normal-variation";
    case RenderMode::BudgetMargin: return "budget-margin";
    case RenderMode::SlopeHeatmap: return "slope-heatmap";
    }
    return "unknown";
}

const char* shading_normal_mode_name(ShadingNormalMode mode) {
    switch (mode) {
    case ShadingNormalMode::Auto: return "auto";
    case ShadingNormalMode::Geometric: return "geometric";
    case ShadingNormalMode::Smooth: return "smooth";
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
