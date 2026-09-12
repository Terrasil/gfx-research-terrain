#pragma once

#include <filesystem>

namespace gfx::terrain {

enum class ResearchMethod {
    Reference,
    DistanceLod,
    ErrorBounded,
    ErrorBoundedFeatureAware
};

enum class SurfaceScene {
    SmoothHills,
    SharpRidge,
    MixedFrequency,
    CliffBand
};

enum class BenchmarkMode {
    Static,
    Temporal,
    Null
};

struct ExperimentConfig {
    int width = 1600;
    int height = 900;
    int patches_per_side = 8;
    int max_cells_per_patch = 32;
    int lod_count = 5;
    int error_samples_per_side = 65;
    float world_size = 16.0f;

    float error_budget_px = 1.0f;
    float feature_weight = 1.0f;
    float hysteresis = 0.15f;
    int distance_lod_bias = 0;

    bool visualize_lod = false;
    bool wireframe = false;
    bool publication_quick = false;
    bool publication_suite = false;
    bool save_all_captures = false;

    int warmup_frames = 60;
    int sample_frames = 180;
    std::filesystem::path output_dir = "results/publication";
};

[[nodiscard]] const char* method_name(ResearchMethod method);
[[nodiscard]] const char* scene_name(SurfaceScene scene);
[[nodiscard]] const char* benchmark_mode_name(BenchmarkMode mode);

}
