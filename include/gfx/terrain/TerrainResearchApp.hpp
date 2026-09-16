#pragma once

#include <gfx/terrain/AdaptiveTerrainMesh.hpp>
#include <gfx/terrain/BenchmarkRunner.hpp>
#include <gfx/terrain/ErrorController.hpp>
#include <gfx/terrain/ExperimentConfig.hpp>
#include <gfx/terrain/Heightfield.hpp>
#include <gfx/terrain/GpuTessellatedTerrain.hpp>
#include <gfx/terrain/PatchSet.hpp>
#include <gfx/terrain/RenderStats.hpp>
#include <gfx/terrain/TerrainTextures.hpp>

#include <gfx/research/application.hpp>
#include <gfx/research/framebuffer.hpp>
#include <gfx/research/fullscreen_triangle.hpp>
#include <gfx/research/gpu_timer.hpp>
#include <gfx/research/orbit_camera.hpp>
#include <gfx/research/shader.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace gfx::terrain {

enum class GuiTestSuite {
    None,
    Validation,
    DoctoralQuick,
    DoctoralFull,
    PbrFigures,
    RealDemValidation
};

enum class GuiTestStatus {
    Idle,
    Running,
    Passed,
    Failed,
    Cancelled
};

class TerrainResearchApp final : public gfx::research::Application {
public:
    explicit TerrainResearchApp(ExperimentConfig config);
    ~TerrainResearchApp() override;

private:
    bool on_init() override;
    void on_frame(const gfx::research::FrameInfo& frame) override;
    void on_gui(const gfx::research::FrameInfo& frame) override;
    void on_resize(int width, int height) override;

    void rebuild_scene();
    void ensure_cpu_scene_structures();
    [[nodiscard]] int active_tessellation_patch_grid() const;
    void write_scene_rebuild_stage(const char* stage) const;
    void write_dem_runtime_stage(const char* stage, int patch_grid = -1) const;
    void upload_heightfield_texture();
    void ensure_framebuffers(int width, int height);
    void apply_benchmark_case(const BenchmarkCase& test_case);
    void update_benchmark_frame_state();
    void update_camera_controls(const gfx::research::FrameInfo& frame);
    void apply_camera_preset(int preset_index);
    void reset_camera_to_scene();
    void start_gui_test_suite(GuiTestSuite suite, const gfx::research::FrameInfo& frame);
    void finish_gui_test_suite(const gfx::research::FrameInfo& frame, bool cancelled = false);
    void restore_interactive_state();
    [[nodiscard]] int run_python_tool(const std::filesystem::path& script, const std::filesystem::path* argument) const;

    RenderStats render_surface(
        ResearchMethod method,
        gfx::research::Framebuffer& target,
        int width,
        int height,
        bool time_gpu,
        bool force_finest = false);

    void present_texture(unsigned int texture, int width, int height);
    void capture_quality(const BenchmarkCase& test_case);
    void save_case_captures(const BenchmarkCase& test_case);
    void save_rgb_error_capture(const BenchmarkCase& test_case, const std::filesystem::path& capture_dir, const std::string& stem);
    void write_lod_error_table(SurfaceScene scene);

    [[nodiscard]] RuntimeInfo runtime_info(int width, int height) const;
    [[nodiscard]] std::vector<int> select_lods(
        ResearchMethod method,
        const SelectionContext& context,
        bool force_finest,
        RenderStats& stats);

    ExperimentConfig experiment_config_;
    Heightfield heightfield_;
    PatchSet patch_set_;
    AdaptiveTerrainMesh adaptive_mesh_;
    ErrorController controller_;
    BenchmarkRunner benchmark_;

    gfx::research::OrbitCamera camera_;
    gfx::research::Shader surface_shader_;
    gfx::research::Shader tessellation_shader_;
    gfx::research::Shader present_shader_;
    gfx::research::Shader difference_shader_;
    gfx::research::Framebuffer method_fbo_;
    gfx::research::Framebuffer reference_fbo_;
    gfx::research::Framebuffer difference_fbo_;
    gfx::research::GpuTimer gpu_timer_;
    gfx::research::FullscreenTriangle fullscreen_triangle_;
    TerrainTextures terrain_textures_;
    GpuTessellatedTerrain tessellated_terrain_;
    unsigned int heightfield_texture_ = 0;
    unsigned int heightfield_validity_texture_ = 0;

    ResearchMethod method_ = ResearchMethod::TessellationContextAware;
    SurfaceScene scene_ = SurfaceScene::GlacialValley;
    RenderStats last_render_stats_{};
    double last_gpu_ms_ = 0.0;
    bool framebuffers_ready_ = false;
    bool capture_render_mode_override_enabled_ = false;
    RenderMode capture_render_mode_ = RenderMode::Scientific;
    bool benchmark_started_ = false;
    std::size_t applied_benchmark_case_ = static_cast<std::size_t>(-1);
    std::array<bool, surface_scene_count> scene_error_table_written_{};
    bool cpu_scene_structures_ready_ = false;
    bool dem_first_draw_verified_ = false;
    std::string scene_load_error_;

    bool camera_cursor_initialized_ = false;
    double previous_cursor_x_ = 0.0;
    double previous_cursor_y_ = 0.0;
    float camera_speed_multiplier_ = 1.0f;
    int camera_preset_ = 0;
    float camera_idle_seconds_ = 1.0f;
    bool camera_moved_this_frame_ = false;
    bool defer_cpu_adaptation_while_moving_ = true;

    GuiTestSuite gui_test_suite_ = GuiTestSuite::None;
    GuiTestStatus gui_test_status_ = GuiTestStatus::Idle;
    std::string gui_test_message_ = "Ready";
    std::filesystem::path gui_test_output_dir_;
    bool gui_validation_passed_ = false;
    ExperimentConfig gui_restore_config_{};
    ResearchMethod gui_restore_method_ = ResearchMethod::TessellationContextAware;
    SurfaceScene gui_restore_scene_ = SurfaceScene::GlacialValley;
    int gui_restore_camera_preset_ = 0;
    std::array<float, 3> gui_restore_camera_target_{};
    float gui_restore_camera_distance_ = 1.0f;
    float gui_restore_camera_yaw_ = 0.0f;
    float gui_restore_camera_pitch_ = 0.0f;
    float gui_restore_camera_fov_ = 55.0f;
};

}
