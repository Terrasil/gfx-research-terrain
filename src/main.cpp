#include <gfx/terrain/ExperimentConfig.hpp>
#include <gfx/terrain/TerrainResearchApp.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace gfx::terrain {

namespace {

int parse_int(std::string_view value, int fallback) {
    const std::string text(value);
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    return end != text.c_str() ? static_cast<int>(parsed) : fallback;
}

float parse_float(std::string_view value, float fallback) {
    const std::string text(value);
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    return end != text.c_str() ? parsed : fallback;
}

}

ExperimentConfig parse_arguments(int argc, char** argv) {
    ExperimentConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--publication-quick") {
            config.publication_quick = true;
            config.publication_suite = false;
        } else if (arg == "--publication-suite") {
            config.publication_suite = true;
            config.publication_quick = false;
        } else if (arg == "--save-all-captures") {
            config.save_all_captures = true;
        } else if (arg == "--visualize-lod") {
            config.visualize_lod = true;
        } else if (arg == "--wireframe") {
            config.wireframe = true;
        } else if (arg.starts_with("--benchmark-output=")) {
            config.output_dir = std::filesystem::path(arg.substr(19));
        } else if (arg.starts_with("--error-budget=")) {
            config.error_budget_px = parse_float(arg.substr(15), config.error_budget_px);
        } else if (arg.starts_with("--feature-weight=")) {
            config.feature_weight = parse_float(arg.substr(17), config.feature_weight);
        } else if (arg.starts_with("--hysteresis=")) {
            config.hysteresis = parse_float(arg.substr(13), config.hysteresis);
        } else if (arg.starts_with("--distance-bias=")) {
            config.distance_lod_bias = parse_int(arg.substr(16), config.distance_lod_bias);
        } else if (arg.starts_with("--patches=")) {
            config.patches_per_side = parse_int(arg.substr(10), config.patches_per_side);
        } else if (arg.starts_with("--max-cells=")) {
            config.max_cells_per_patch = parse_int(arg.substr(12), config.max_cells_per_patch);
        } else if (arg.starts_with("--lod-count=")) {
            config.lod_count = parse_int(arg.substr(12), config.lod_count);
        } else if (arg.starts_with("--error-samples=")) {
            config.error_samples_per_side = parse_int(arg.substr(16), config.error_samples_per_side);
        } else if (arg.starts_with("--warmup=")) {
            config.warmup_frames = parse_int(arg.substr(9), config.warmup_frames);
        } else if (arg.starts_with("--samples=")) {
            config.sample_frames = parse_int(arg.substr(10), config.sample_frames);
        }
    }
    return config;
}

}

int main(int argc, char** argv) {
    return gfx::terrain::TerrainResearchApp(gfx::terrain::parse_arguments(argc, argv)).run();
}
