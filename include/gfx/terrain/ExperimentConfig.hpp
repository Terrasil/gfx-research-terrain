#pragma once

#include <filesystem>

namespace gfx::terrain {

enum class ResearchMethod {
    Reference,
    DistanceLod,
    ErrorBounded,
    ErrorBoundedFeatureAware,
    AdaptiveDistance,
    AdaptiveError,
    AdaptiveErrorFeatureAware,
    TessellationDistance,
    TessellationError,
    TessellationErrorFeatureAware,
    TessellationVarianceBaseline,
    TessellationNormalBound,
    TessellationContextAware
};

enum class SurfaceScene {
    SmoothHills,
    SharpRidge,
    MixedFrequency,
    CliffBand,
    ErodedMountain,
    PlateauCanyon,
    AlpineEscarpment,
    GlacialValley,
    RiverBasin,
    Badlands,
    FracturedHighlands,
    MountStHelensDem
};

inline constexpr int surface_scene_count = 12;

enum class RenderMode {
    Scientific,
    Realistic,
    LodHeatmap,
    ErrorHeatmap,
    NormalVariation,
    BudgetMargin,
    SlopeHeatmap
};

enum class BenchmarkMode {
    Static,
    Temporal,
    Null
};

enum class ShadingNormalMode {
    Auto,
    Geometric,
    Smooth
};

struct ExperimentConfig {
    int width = 1600;
    int height = 900;
    int patches_per_side = 8;
    int max_cells_per_patch = 64;
    int lod_count = 6;
    int error_samples_per_side = 65;
    int adaptive_max_level = 9;
    int tessellation_patch_grid = 32;
    float tessellation_max_level = 16.0f;
    float world_size = 16.0f;

    float error_budget_px = 1.0f;
    float feature_weight = 1.0f;
    float normal_budget_degrees = 5.0f;
    float radiance_budget = 0.025f;
    float scientific_roughness = 0.45f;
    float light_azimuth_degrees = -42.0f;
    float light_elevation_degrees = 52.0f;
    float hysteresis = 0.15f;
    float morph_band = 0.75f;
    int distance_lod_bias = 0;

    RenderMode render_mode = RenderMode::Realistic;
    ShadingNormalMode shading_normal_mode = ShadingNormalMode::Auto;
    bool wireframe = false;
    bool validation_suite = false;
    bool publication_quick = false;
    bool publication_suite = false;
    bool thesis_figure_suite = false;
    bool thesis_pbr_figure_suite = false;
    bool real_dem_validation_suite = false;
    bool save_all_captures = false;

    int warmup_frames = 60;
    int sample_frames = 180;
    std::filesystem::path output_dir = "results/publication";
};

[[nodiscard]] const char* method_name(ResearchMethod method);
[[nodiscard]] const char* scene_name(SurfaceScene scene);
[[nodiscard]] const char* scene_description(SurfaceScene scene);
[[nodiscard]] const char* benchmark_mode_name(BenchmarkMode mode);
[[nodiscard]] const char* render_mode_name(RenderMode mode);
[[nodiscard]] const char* shading_normal_mode_name(ShadingNormalMode mode);

}
