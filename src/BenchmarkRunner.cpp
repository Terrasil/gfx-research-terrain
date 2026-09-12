#include <utility>
#include <gfx/terrain/BenchmarkRunner.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <numbers>

#ifndef GFX_TERRAIN_GIT_COMMIT
#define GFX_TERRAIN_GIT_COMMIT "unknown"
#endif
#ifndef GFX_RESEARCH_BASE_REVISION
#define GFX_RESEARCH_BASE_REVISION "unknown"
#endif
#ifndef GFX_TERRAIN_BUILD_TYPE
#define GFX_TERRAIN_BUILD_TYPE "unknown"
#endif
#ifndef GFX_TERRAIN_COMPILER_ID
#define GFX_TERRAIN_COMPILER_ID "unknown"
#endif
#ifndef GFX_TERRAIN_COMPILER_VERSION
#define GFX_TERRAIN_COMPILER_VERSION "unknown"
#endif
#ifndef GFX_TERRAIN_SYSTEM_NAME
#define GFX_TERRAIN_SYSTEM_NAME "unknown"
#endif
#ifndef GFX_TERRAIN_SYSTEM_PROCESSOR
#define GFX_TERRAIN_SYSTEM_PROCESSOR "unknown"
#endif

namespace gfx::terrain {

const char* benchmark_study_name(BenchmarkStudy study) {
    switch (study) {
    case BenchmarkStudy::Primary: return "primary";
    case BenchmarkStudy::ResolutionScaling: return "resolution-scaling";
    case BenchmarkStudy::TemporalStability: return "temporal-stability";
    case BenchmarkStudy::NullEquivalence: return "null-equivalence";
    }
    return "unknown";
}

void BenchmarkRunner::configure(const ExperimentConfig& config) {
    config_ = config;
    warmup_frames_ = config.publication_quick ? std::min(config.warmup_frames, 8) : config.warmup_frames;
    sample_frames_ = config.publication_quick ? std::min(config.sample_frames, 16) : config.sample_frames;
    build_cases(config);
}

void BenchmarkRunner::begin(const RuntimeInfo& runtime_info) {
    if (cases_.empty()) return;
    std::filesystem::create_directories(config_.output_dir);
    case_index_ = 0;
    frame_in_case_ = 0;
    active_ = true;
    finished_ = false;
    write_manifest(runtime_info);
    ensure_csv_open();
}

bool BenchmarkRunner::should_capture_quality() const {
    if (!active_) return false;
    return frame_in_case_ == warmup_frames_ + sample_frames_ - 1;
}

float BenchmarkRunner::current_camera_distance() const {
    if (!active_) return 0.0f;
    const auto& c = current_case();
    if (c.mode != BenchmarkMode::Temporal || c.camera_motion_amplitude == 0.0f) {
        return c.camera_distance;
    }

    const int total = std::max(1, warmup_frames_ + sample_frames_ - 1);
    const double t = static_cast<double>(frame_in_case_) / static_cast<double>(total);
    const double phase = 2.0 * std::numbers::pi * static_cast<double>(c.camera_motion_cycles) * t;
    return c.camera_distance + c.camera_motion_amplitude * static_cast<float>(std::sin(phase));
}

void BenchmarkRunner::record_frame(double gpu_ms, const RenderStats& stats, float camera_distance) {
    if (!active_ || in_warmup()) return;
    const auto& c = current_case();
    raw_csv_
        << case_index_ << ','
        << benchmark_study_name(c.study) << ','
        << benchmark_mode_name(c.mode) << ','
        << scene_name(c.scene) << ','
        << method_name(c.method) << ','
        << camera_distance << ','
        << c.camera_distance << ','
        << c.camera_motion_amplitude << ','
        << c.error_budget_px << ','
        << c.distance_lod_bias << ','
        << c.hysteresis << ','
        << c.width << ',' << c.height << ','
        << frame_in_case_ << ','
        << std::setprecision(10) << gpu_ms << ','
        << stats.triangles << ','
        << stats.lod_transitions << ','
        << stats.max_geometric_error_px << ','
        << stats.max_controller_error_px << ','
        << stats.mean_geometric_error_px;
    for (int lod = 0; lod < max_lod_count; ++lod) raw_csv_ << ',' << stats.lod_histogram[static_cast<std::size_t>(lod)];
    raw_csv_ << '\n';
}

void BenchmarkRunner::record_quality(
    const ImageMetrics& image,
    const DepthMetrics& depth,
    const EqualityMetrics& equality) {

    if (!active_) return;
    const auto& c = current_case();
    quality_csv_
        << case_index_ << ','
        << benchmark_study_name(c.study) << ','
        << benchmark_mode_name(c.mode) << ','
        << scene_name(c.scene) << ','
        << method_name(c.method) << ','
        << current_camera_distance() << ','
        << c.error_budget_px << ','
        << c.distance_lod_bias << ','
        << c.hysteresis << ','
        << c.width << ',' << c.height << ','
        << std::setprecision(10)
        << image.mae << ','
        << image.rmse << ','
        << image.relative_rmse << ','
        << image.foreground_rmse << ','
        << image.psnr << ','
        << image.max_abs_error << ','
        << depth.mae << ','
        << depth.rmse << ','
        << depth.max_abs_error << ','
        << (equality.rgba_exact ? 1 : 0) << ','
        << (equality.depth_exact ? 1 : 0) << '\n';
}

bool BenchmarkRunner::advance() {
    if (!active_) return false;
    ++frame_in_case_;
    if (frame_in_case_ < warmup_frames_ + sample_frames_) return false;

    frame_in_case_ = 0;
    ++case_index_;
    if (case_index_ < cases_.size()) return true;

    finalize();
    return true;
}

void BenchmarkRunner::finalize() {
    if (!active_) return;
    active_ = false;
    finished_ = true;
    if (raw_csv_.is_open()) raw_csv_.close();
    if (quality_csv_.is_open()) quality_csv_.close();
}

void BenchmarkRunner::build_cases(const ExperimentConfig&) {
    cases_.clear();
    if (config_.publication_quick) build_quick_cases();
    else build_full_cases();
}

void BenchmarkRunner::build_quick_cases() {
    const SurfaceScene scenes[] = {SurfaceScene::SmoothHills, SurfaceScene::SharpRidge};
    const float distances[] = {6.0f, 14.0f};

    for (const auto scene : scenes) {
        for (const float distance : distances) {
            for (const int bias : {0, 1}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::Primary;
                c.scene = scene;
                c.method = ResearchMethod::DistanceLod;
                c.camera_distance = distance;
                c.distance_lod_bias = bias;
                c.width = 1280;
                c.height = 720;
                c.capture_images = bias == 0 && distance == 6.0f;
                cases_.push_back(c);
            }
            for (const float budget : {0.5f, 1.0f}) {
                for (const ResearchMethod method : {ResearchMethod::ErrorBounded, ResearchMethod::ErrorBoundedFeatureAware}) {
                    BenchmarkCase c;
                    c.study = BenchmarkStudy::Primary;
                    c.scene = scene;
                    c.method = method;
                    c.camera_distance = distance;
                    c.error_budget_px = budget;
                    c.width = 1280;
                    c.height = 720;
                    c.capture_images = method == ResearchMethod::ErrorBoundedFeatureAware && budget == 1.0f && distance == 6.0f;
                    cases_.push_back(c);
                }
            }
        }
    }

    for (const float hysteresis : {0.0f, 0.15f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::TemporalStability;
        c.mode = BenchmarkMode::Temporal;
        c.scene = SurfaceScene::SharpRidge;
        c.method = ResearchMethod::ErrorBoundedFeatureAware;
        c.camera_distance = 8.0f;
        c.camera_motion_amplitude = 2.0f;
        c.camera_motion_cycles = 2.0f;
        c.error_budget_px = 1.0f;
        c.hysteresis = hysteresis;
        c.width = 1280;
        c.height = 720;
        cases_.push_back(c);
    }

    for (const auto scene : scenes) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::NullEquivalence;
        c.mode = BenchmarkMode::Null;
        c.scene = scene;
        c.method = ResearchMethod::ErrorBoundedFeatureAware;
        c.force_finest = true;
        c.width = 1280;
        c.height = 720;
        c.capture_images = true;
        cases_.push_back(c);
    }
}

void BenchmarkRunner::build_full_cases() {
    const SurfaceScene scenes[] = {
        SurfaceScene::SmoothHills,
        SurfaceScene::SharpRidge,
        SurfaceScene::MixedFrequency,
        SurfaceScene::CliffBand
    };
    const float distances[] = {5.0f, 8.0f, 12.0f, 20.0f};

    for (const auto scene : scenes) {
        for (const float distance : distances) {
            for (const int bias : {-1, 0, 1, 2}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::Primary;
                c.scene = scene;
                c.method = ResearchMethod::DistanceLod;
                c.camera_distance = distance;
                c.distance_lod_bias = bias;
                c.capture_images = scene == SurfaceScene::MixedFrequency && distance == 8.0f && bias == 0;
                cases_.push_back(c);
            }

            for (const float budget : {0.25f, 0.5f, 1.0f, 2.0f}) {
                for (const ResearchMethod method : {ResearchMethod::ErrorBounded, ResearchMethod::ErrorBoundedFeatureAware}) {
                    BenchmarkCase c;
                    c.study = BenchmarkStudy::Primary;
                    c.scene = scene;
                    c.method = method;
                    c.camera_distance = distance;
                    c.error_budget_px = budget;
                    c.capture_images =
                        (scene == SurfaceScene::MixedFrequency || scene == SurfaceScene::SharpRidge) &&
                        distance == 8.0f && budget == 1.0f;
                    cases_.push_back(c);
                }
            }
        }
    }

    const std::pair<int, int> resolutions[] = {{1280, 720}, {1920, 1080}, {2560, 1440}};
    for (const auto scene : {SurfaceScene::SharpRidge, SurfaceScene::MixedFrequency}) {
        for (const auto& [width, height] : resolutions) {
            for (const ResearchMethod method : {
                     ResearchMethod::DistanceLod,
                     ResearchMethod::ErrorBounded,
                     ResearchMethod::ErrorBoundedFeatureAware}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::ResolutionScaling;
                c.scene = scene;
                c.method = method;
                c.camera_distance = 8.0f;
                c.error_budget_px = 1.0f;
                c.distance_lod_bias = 0;
                c.width = width;
                c.height = height;
                cases_.push_back(c);
            }
        }
    }

    for (const auto scene : {SurfaceScene::SharpRidge, SurfaceScene::MixedFrequency}) {
        for (const ResearchMethod method : {ResearchMethod::ErrorBounded, ResearchMethod::ErrorBoundedFeatureAware}) {
            for (const float hysteresis : {0.0f, 0.15f}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::TemporalStability;
                c.mode = BenchmarkMode::Temporal;
                c.scene = scene;
                c.method = method;
                c.camera_distance = 8.0f;
                c.camera_motion_amplitude = 2.5f;
                c.camera_motion_cycles = 4.0f;
                c.error_budget_px = 1.0f;
                c.hysteresis = hysteresis;
                c.width = 1920;
                c.height = 1080;
                cases_.push_back(c);
            }
        }
    }

    for (const auto scene : scenes) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::NullEquivalence;
        c.mode = BenchmarkMode::Null;
        c.scene = scene;
        c.method = ResearchMethod::ErrorBoundedFeatureAware;
        c.force_finest = true;
        c.width = 1920;
        c.height = 1080;
        c.capture_images = true;
        cases_.push_back(c);
    }
}

void BenchmarkRunner::write_manifest(const RuntimeInfo& runtime_info) const {
    std::ofstream out(config_.output_dir / "manifest.txt");
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    out << "experiment=gfx-research-terrain\n";
    out << "timestamp_utc=" << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ") << '\n';
    out << "git_commit=" << GFX_TERRAIN_GIT_COMMIT << '\n';
    out << "base_revision=" << GFX_RESEARCH_BASE_REVISION << '\n';
    out << "build_type=" << GFX_TERRAIN_BUILD_TYPE << '\n';
    out << "compiler=" << GFX_TERRAIN_COMPILER_ID << ' ' << GFX_TERRAIN_COMPILER_VERSION << '\n';
    out << "system=" << GFX_TERRAIN_SYSTEM_NAME << '\n';
    out << "processor=" << GFX_TERRAIN_SYSTEM_PROCESSOR << '\n';
    out << "gl_vendor=" << runtime_info.gl_vendor << '\n';
    out << "gl_renderer=" << runtime_info.gl_renderer << '\n';
    out << "gl_version=" << runtime_info.gl_version << '\n';
    out << "initial_framebuffer=" << runtime_info.initial_framebuffer_width << 'x'
        << runtime_info.initial_framebuffer_height << '\n';
    out << "patches_per_side=" << config_.patches_per_side << '\n';
    out << "max_cells_per_patch=" << config_.max_cells_per_patch << '\n';
    out << "lod_count=" << config_.lod_count << '\n';
    out << "error_samples_per_side=" << config_.error_samples_per_side << '\n';
    out << "world_size=" << config_.world_size << '\n';
    out << "feature_weight=" << config_.feature_weight << '\n';
    out << "default_hysteresis=" << config_.hysteresis << '\n';
    out << "warmup_frames=" << warmup_frames_ << '\n';
    out << "sample_frames=" << sample_frames_ << '\n';
    out << "case_count=" << cases_.size() << '\n';
    out << "seam_control=vertex-shader edge snapping to coarser neighboring boundary\n";
    out << "reference=finest available patch mesh at every patch\n";
    out << "timing=asynchronous GPU timestamp queries from gfx-research-base\n";
}

void BenchmarkRunner::ensure_csv_open() {
    raw_csv_.open(config_.output_dir / "raw.csv", std::ios::trunc);
    quality_csv_.open(config_.output_dir / "quality.csv", std::ios::trunc);

    raw_csv_ << "case,study,mode,scene,method,camera_distance,base_distance,motion_amplitude,error_budget_px,"
                "distance_lod_bias,hysteresis,width,height,frame,gpu_ms,triangles,lod_transitions,"
                "max_geometric_error_px,max_controller_error_px,mean_geometric_error_px";
    for (int lod = 0; lod < max_lod_count; ++lod) raw_csv_ << ",lod" << lod << "_patches";
    raw_csv_ << '\n';

    quality_csv_ << "case,study,mode,scene,method,camera_distance,error_budget_px,distance_lod_bias,hysteresis,"
                    "width,height,image_mae,image_rmse,image_relative_rmse,image_foreground_rmse,image_psnr,"
                    "image_max_abs,depth_mae,depth_rmse,depth_max_abs,rgba_exact,depth_exact\n";
}

}
