#pragma once

#include <gfx/terrain/ExperimentConfig.hpp>
#include <gfx/terrain/Metrics.hpp>
#include <gfx/terrain/RenderStats.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gfx::terrain {

enum class BenchmarkStudy {
    HarnessValidation,
    EstimatorCalibration,
    StrongBaseline,
    ContextSensitivity,
    SilhouetteStress,
    ResolutionScaling,
    ViewAngleScaling,
    FovScaling,
    ConstraintIsolation,
    TemporalStability,
    NegativeControl,
    NullEquivalence,
    ThesisFigure,
    ThesisCamera,
    ShadingNormalStudy,
    PbrFigure,
    RealDemValidation
};

struct BenchmarkCase {
    BenchmarkStudy study = BenchmarkStudy::StrongBaseline;
    BenchmarkMode mode = BenchmarkMode::Static;
    SurfaceScene scene = SurfaceScene::MixedFrequency;
    ResearchMethod method = ResearchMethod::TessellationContextAware;
    RenderMode benchmark_render_mode = RenderMode::Scientific;
    ShadingNormalMode shading_normal_mode = ShadingNormalMode::Auto;

    float camera_distance = 8.0f;
    float camera_yaw = 0.68f;
    float camera_pitch = 0.52f;
    float camera_fov_degrees = 55.0f;
    float camera_target_x = 0.0f;
    float camera_target_z = 0.0f;
    float camera_target_height_offset = 0.10f;
    float camera_motion_amplitude = 0.0f;
    float camera_motion_cycles = 0.0f;
    float error_budget_px = 1.0f;
    float normal_budget_degrees = 5.0f;
    float radiance_budget = 0.025f;
    float scientific_roughness = 0.45f;
    float light_azimuth_degrees = -42.0f;
    float light_elevation_degrees = 52.0f;
    float hysteresis = 0.15f;
    float morph_band = 0.75f;
    int distance_lod_bias = 0;

    int width = 1920;
    int height = 1080;
    bool force_finest = false;
    bool capture_images = false;
    bool capture_diagnostics = false;
};

struct RuntimeInfo {
    std::string gl_vendor;
    std::string gl_renderer;
    std::string gl_version;
    int initial_framebuffer_width = 0;
    int initial_framebuffer_height = 0;
};

class BenchmarkRunner {
public:
    void configure(const ExperimentConfig& config);
    void begin(const RuntimeInfo& runtime_info);

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool finished() const { return finished_; }
    [[nodiscard]] std::size_t case_index() const { return case_index_; }
    [[nodiscard]] std::size_t case_count() const { return cases_.size(); }
    [[nodiscard]] const BenchmarkCase& current_case() const { return cases_.at(case_index_); }
    [[nodiscard]] int warmup_frames() const { return warmup_frames_; }
    [[nodiscard]] int sample_frames() const { return sample_frames_; }
    [[nodiscard]] int frame_in_case() const { return frame_in_case_; }
    [[nodiscard]] bool in_warmup() const { return frame_in_case_ < warmup_frames_; }
    [[nodiscard]] bool should_capture_quality() const;
    [[nodiscard]] float current_camera_distance() const;

    void record_frame(double gpu_ms, const RenderStats& stats, float camera_distance);
    void record_quality(
        const ImageMetrics& image,
        const DepthMetrics& depth,
        const SilhouetteMetrics& silhouette,
        const EqualityMetrics& equality);
    bool advance();
    void finalize();

private:
    void build_cases(const ExperimentConfig& config);
    void build_validation_cases();
    void build_quick_cases();
    void build_full_cases();
    void build_thesis_figure_cases();
    void build_pbr_figure_cases();
    void build_real_dem_validation_cases();
    void write_manifest(const RuntimeInfo& runtime_info) const;
    void ensure_csv_open();

    ExperimentConfig config_{};
    std::vector<BenchmarkCase> cases_;
    std::size_t case_index_ = 0;
    int frame_in_case_ = 0;
    int warmup_frames_ = 0;
    int sample_frames_ = 0;
    bool active_ = false;
    bool finished_ = false;
    std::ofstream raw_csv_;
    std::ofstream quality_csv_;
};

[[nodiscard]] const char* benchmark_study_name(BenchmarkStudy study);

}
