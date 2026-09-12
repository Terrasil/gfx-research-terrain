#pragma once

#include <gfx/terrain/BenchmarkRunner.hpp>
#include <gfx/terrain/ErrorController.hpp>
#include <gfx/terrain/ExperimentConfig.hpp>
#include <gfx/terrain/Heightfield.hpp>
#include <gfx/terrain/PatchSet.hpp>
#include <gfx/terrain/RenderStats.hpp>

#include <gfx/research/application.hpp>
#include <gfx/research/framebuffer.hpp>
#include <gfx/research/fullscreen_triangle.hpp>
#include <gfx/research/gpu_timer.hpp>
#include <gfx/research/orbit_camera.hpp>
#include <gfx/research/shader.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace gfx::terrain {

class TerrainResearchApp final : public gfx::research::Application {
public:
    explicit TerrainResearchApp(ExperimentConfig config);

private:
    bool on_init() override;
    void on_frame(const gfx::research::FrameInfo& frame) override;
    void on_gui(const gfx::research::FrameInfo& frame) override;
    void on_resize(int width, int height) override;

    void rebuild_scene();
    void ensure_framebuffers(int width, int height);
    void apply_benchmark_case(const BenchmarkCase& test_case);
    void update_benchmark_frame_state();

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
    ErrorController controller_;
    BenchmarkRunner benchmark_;

    gfx::research::OrbitCamera camera_;
    gfx::research::Shader surface_shader_;
    gfx::research::Shader present_shader_;
    gfx::research::Framebuffer method_fbo_;
    gfx::research::Framebuffer reference_fbo_;
    gfx::research::GpuTimer gpu_timer_;
    gfx::research::FullscreenTriangle fullscreen_triangle_;

    ResearchMethod method_ = ResearchMethod::ErrorBoundedFeatureAware;
    SurfaceScene scene_ = SurfaceScene::MixedFrequency;
    RenderStats last_render_stats_{};
    double last_gpu_ms_ = 0.0;
    bool framebuffers_ready_ = false;
    bool benchmark_started_ = false;
    std::size_t applied_benchmark_case_ = static_cast<std::size_t>(-1);
    std::array<bool, 4> scene_error_table_written_{};
};

}
