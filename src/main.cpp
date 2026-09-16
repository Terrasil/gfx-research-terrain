#include <exception>
#include <fstream>
#include <iostream>
#include <gfx/terrain/ExperimentConfig.hpp>
#include <gfx/terrain/TerrainResearchApp.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#endif

#ifndef GFX_TERRAIN_SOURCE_DIR
#define GFX_TERRAIN_SOURCE_DIR "."
#endif

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


#if defined(_WIN32)
LONG WINAPI write_windows_crash_log(EXCEPTION_POINTERS* exception_info) {
    try {
        const std::filesystem::path crash_dir = std::filesystem::path(GFX_TERRAIN_SOURCE_DIR) / "results";
        std::filesystem::create_directories(crash_dir);
        std::ofstream out(crash_dir / "last_crash.txt", std::ios::trunc);
        out << "Windows structured exception" << '\n';
        if (exception_info != nullptr && exception_info->ExceptionRecord != nullptr) {
            const void* exception_address = exception_info->ExceptionRecord->ExceptionAddress;
            out << "exception_code=0x" << std::hex
                << static_cast<unsigned long>(exception_info->ExceptionRecord->ExceptionCode) << '\n';
            out << "exception_address=" << exception_address << '\n';

            HMODULE module = nullptr;
            if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(exception_address),
                    &module)) {
                char module_path[MAX_PATH]{};
                if (GetModuleFileNameA(module, module_path, MAX_PATH) != 0) {
                    out << "exception_module=" << module_path << '\n';
                }
            }
        }

        const std::filesystem::path dem_stage_path = crash_dir / "last_dem_runtime.txt";
        std::ifstream dem_stage(dem_stage_path);
        if (dem_stage) {
            out << "--- last_dem_runtime ---" << '\n';
            out << dem_stage.rdbuf();
        }
        out.flush();
    } catch (...) {
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

}

ExperimentConfig parse_arguments(int argc, char** argv) {
    ExperimentConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--validation-suite") {
            config.validation_suite = true;
            config.publication_quick = false;
            config.publication_suite = false;
            config.thesis_figure_suite = false;
            config.thesis_pbr_figure_suite = false;
            config.real_dem_validation_suite = false;
        } else if (arg == "--publication-quick" || arg == "--doctoral-quick") {
            config.validation_suite = false;
            config.publication_quick = true;
            config.publication_suite = false;
            config.thesis_figure_suite = false;
            config.thesis_pbr_figure_suite = false;
            config.real_dem_validation_suite = false;
        } else if (arg == "--publication-suite" || arg == "--doctoral-suite") {
            config.validation_suite = false;
            config.publication_suite = true;
            config.publication_quick = false;
            config.thesis_figure_suite = false;
            config.thesis_pbr_figure_suite = false;
            config.real_dem_validation_suite = false;
        } else if (arg == "--thesis-figures") {
            config.validation_suite = false;
            config.publication_suite = false;
            config.publication_quick = false;
            config.thesis_figure_suite = true;
            config.thesis_pbr_figure_suite = false;
            config.real_dem_validation_suite = false;
        } else if (arg == "--thesis-figures-pbr") {
            config.validation_suite = false;
            config.publication_suite = false;
            config.publication_quick = false;
            config.thesis_figure_suite = false;
            config.thesis_pbr_figure_suite = true;
            config.real_dem_validation_suite = false;
        } else if (arg == "--real-dem-validation") {
            config.validation_suite = false;
            config.publication_suite = false;
            config.publication_quick = false;
            config.thesis_figure_suite = false;
            config.thesis_pbr_figure_suite = false;
            config.real_dem_validation_suite = true;
        } else if (arg == "--save-all-captures") {
            config.save_all_captures = true;
        } else if (arg == "--visualize-lod") {
            config.render_mode = RenderMode::LodHeatmap;
        } else if (arg == "--error-heatmap") {
            config.render_mode = RenderMode::ErrorHeatmap;
        } else if (arg == "--normal-variation") {
            config.render_mode = RenderMode::NormalVariation;
        } else if (arg == "--budget-margin-heatmap") {
            config.render_mode = RenderMode::BudgetMargin;
        } else if (arg == "--slope-heatmap") {
            config.render_mode = RenderMode::SlopeHeatmap;
        } else if (arg == "--scientific") {
            config.render_mode = RenderMode::Scientific;
        } else if (arg == "--realistic") {
            config.render_mode = RenderMode::Realistic;
        } else if (arg == "--auto-normals") {
            config.shading_normal_mode = ShadingNormalMode::Auto;
        } else if (arg == "--geometric-normals") {
            config.shading_normal_mode = ShadingNormalMode::Geometric;
        } else if (arg == "--smooth-normals") {
            config.shading_normal_mode = ShadingNormalMode::Smooth;
        } else if (arg == "--wireframe") {
            config.wireframe = true;
        } else if (arg.starts_with("--benchmark-output=")) {
            config.output_dir = std::filesystem::path(arg.substr(19));
        } else if (arg.starts_with("--error-budget=")) {
            config.error_budget_px = parse_float(arg.substr(15), config.error_budget_px);
        } else if (arg.starts_with("--feature-weight=")) {
            config.feature_weight = parse_float(arg.substr(17), config.feature_weight);
        } else if (arg.starts_with("--normal-budget-deg=")) {
            config.normal_budget_degrees = parse_float(arg.substr(20), config.normal_budget_degrees);
        } else if (arg.starts_with("--radiance-budget=")) {
            config.radiance_budget = parse_float(arg.substr(18), config.radiance_budget);
        } else if (arg.starts_with("--scientific-roughness=")) {
            config.scientific_roughness = parse_float(arg.substr(23), config.scientific_roughness);
        } else if (arg.starts_with("--light-azimuth=")) {
            config.light_azimuth_degrees = parse_float(arg.substr(16), config.light_azimuth_degrees);
        } else if (arg.starts_with("--light-elevation=")) {
            config.light_elevation_degrees = parse_float(arg.substr(18), config.light_elevation_degrees);
        } else if (arg.starts_with("--hysteresis=")) {
            config.hysteresis = parse_float(arg.substr(13), config.hysteresis);
        } else if (arg.starts_with("--distance-bias=")) {
            config.distance_lod_bias = parse_int(arg.substr(16), config.distance_lod_bias);
        } else if (arg.starts_with("--morph-band=")) {
            config.morph_band = parse_float(arg.substr(13), config.morph_band);
        } else if (arg.starts_with("--adaptive-max-level=")) {
            config.adaptive_max_level = parse_int(arg.substr(21), config.adaptive_max_level);
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
#if defined(_WIN32)
    SetUnhandledExceptionFilter(gfx::terrain::write_windows_crash_log);
#endif
    try {
        return gfx::terrain::TerrainResearchApp(gfx::terrain::parse_arguments(argc, argv)).run();
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        try {
            const std::filesystem::path crash_dir = std::filesystem::path(GFX_TERRAIN_SOURCE_DIR) / "results";
            std::filesystem::create_directories(crash_dir);
            std::ofstream out(crash_dir / "last_crash.txt", std::ios::trunc);
            out << error.what() << '\n';
        } catch (...) {
        }
        return 1;
    } catch (...) {
        std::cerr << "Fatal error: unknown exception\n";
        return 2;
    }
}
