#include <gfx/terrain/BenchmarkRunner.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <numbers>
#include <utility>

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

namespace {

void apply_overview_camera(BenchmarkCase& c, SurfaceScene scene) {
    c.camera_fov_degrees = 52.0f;
    c.camera_target_x = 0.0f;
    c.camera_target_z = 0.0f;
    c.camera_target_height_offset = 0.08f;

    switch (scene) {
    case SurfaceScene::SmoothHills:
        c.camera_distance = 18.5f;
        c.camera_yaw = 0.66f;
        c.camera_pitch = 0.55f;
        break;
    case SurfaceScene::SharpRidge:
        c.camera_distance = 17.5f;
        c.camera_yaw = 0.54f;
        c.camera_pitch = 0.38f;
        break;
    case SurfaceScene::MixedFrequency:
        c.camera_distance = 18.5f;
        c.camera_yaw = 0.70f;
        c.camera_pitch = 0.52f;
        break;
    case SurfaceScene::CliffBand:
        c.camera_distance = 15.0f;
        c.camera_yaw = 2.18f;
        c.camera_pitch = 0.37f;
        c.camera_fov_degrees = 55.0f;
        c.camera_target_z = 0.02f;
        c.camera_target_height_offset = 0.08f;
        break;
    case SurfaceScene::AlpineEscarpment:
        c.camera_distance = 15.0f;
        c.camera_yaw = 2.32f;
        c.camera_pitch = 0.33f;
        c.camera_fov_degrees = 55.0f;
        c.camera_target_z = 0.02f;
        c.camera_target_height_offset = 0.08f;
        break;
    case SurfaceScene::GlacialValley:
        c.camera_distance = 15.0f;
        c.camera_yaw = 0.63f;
        c.camera_pitch = 0.49f;
        c.camera_fov_degrees = 55.0f;
        c.camera_target_z = 0.0f;
        c.camera_target_height_offset = 0.08f;
        break;
    case SurfaceScene::Badlands:
        c.camera_distance = 20.5f;
        c.camera_yaw = 0.82f;
        c.camera_pitch = 0.56f;
        break;
    case SurfaceScene::FracturedHighlands:
        c.camera_distance = 20.5f;
        c.camera_yaw = 0.74f;
        c.camera_pitch = 0.54f;
        break;
    case SurfaceScene::MountStHelensDem:
        // The dense 19.2 km crop is centered on the summit and contains no survey-footprint holes.
        c.camera_distance = 17.3f;
        c.camera_yaw = 2.34f;
        c.camera_pitch = 0.48f;
        c.camera_fov_degrees = 52.0f;
        c.camera_target_x = 0.0f;
        c.camera_target_z = 0.0f;
        c.camera_target_height_offset = 0.10f;
        break;
    default:
        c.camera_distance = 19.0f;
        c.camera_yaw = 0.68f;
        c.camera_pitch = 0.52f;
        break;
    }
}

}


const char* benchmark_study_name(BenchmarkStudy study) {
    switch (study) {
    case BenchmarkStudy::HarnessValidation: return "harness-validation";
    case BenchmarkStudy::EstimatorCalibration: return "estimator-calibration";
    case BenchmarkStudy::StrongBaseline: return "strong-baseline";
    case BenchmarkStudy::ContextSensitivity: return "context-sensitivity";
    case BenchmarkStudy::SilhouetteStress: return "silhouette-stress";
    case BenchmarkStudy::ResolutionScaling: return "resolution-scaling";
    case BenchmarkStudy::ViewAngleScaling: return "view-angle-scaling";
    case BenchmarkStudy::FovScaling: return "fov-scaling";
    case BenchmarkStudy::ConstraintIsolation: return "constraint-isolation";
    case BenchmarkStudy::TemporalStability: return "temporal-stability";
    case BenchmarkStudy::NegativeControl: return "negative-control";
    case BenchmarkStudy::NullEquivalence: return "null-equivalence";
    case BenchmarkStudy::ThesisFigure: return "thesis-figure";
    case BenchmarkStudy::ThesisCamera: return "thesis-camera";
    case BenchmarkStudy::ShadingNormalStudy: return "shading-normal-study";
    case BenchmarkStudy::PbrFigure: return "pbr-figure";
    case BenchmarkStudy::RealDemValidation: return "real-dem-validation";
    }
    return "unknown";
}

void BenchmarkRunner::configure(const ExperimentConfig& config) {
    config_ = config;
    if (config.validation_suite) {
        warmup_frames_ = std::min(config.warmup_frames, 4);
        sample_frames_ = std::min(config.sample_frames, 8);
    } else if (config.publication_quick) {
        warmup_frames_ = std::min(config.warmup_frames, 8);
        sample_frames_ = std::min(config.sample_frames, 20);
    } else if (config.thesis_figure_suite) {
        warmup_frames_ = std::min(config.warmup_frames, 8);
        sample_frames_ = std::min(config.sample_frames, 24);
    } else {
        warmup_frames_ = config.warmup_frames;
        sample_frames_ = config.sample_frames;
    }
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
    if (c.mode != BenchmarkMode::Temporal || c.camera_motion_amplitude == 0.0f) return c.camera_distance;

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
        << render_mode_name(c.benchmark_render_mode) << ','
        << shading_normal_mode_name(c.shading_normal_mode) << ','
        << camera_distance << ','
        << c.camera_distance << ','
        << c.camera_yaw << ','
        << c.camera_pitch << ','
        << c.camera_fov_degrees << ','
        << c.camera_target_x << ','
        << c.camera_target_z << ','
        << c.camera_target_height_offset << ','
        << c.camera_motion_amplitude << ','
        << c.error_budget_px << ','
        << c.normal_budget_degrees << ','
        << c.radiance_budget << ','
        << c.scientific_roughness << ','
        << c.light_azimuth_degrees << ','
        << c.light_elevation_degrees << ','
        << c.distance_lod_bias << ','
        << c.hysteresis << ','
        << c.morph_band << ','
        << c.width << ',' << c.height << ','
        << frame_in_case_ << ','
        << std::setprecision(10) << gpu_ms << ','
        << stats.triangles << ','
        << stats.lod_transitions << ','
        << stats.max_geometric_error_px << ','
        << stats.max_controller_error_px << ','
        << stats.mean_geometric_error_px << ','
        << stats.adaptive_leaf_count << ','
        << stats.morphed_vertices << ','
        << stats.saturated_regions << ','
        << stats.mean_refinement_level << ','
        << stats.adaptation_cpu_ms << ','
        << stats.topology_open_internal_edges << ','
        << stats.topology_nonmanifold_edges;
    for (int lod = 0; lod < max_lod_count; ++lod) {
        raw_csv_ << ',' << stats.lod_histogram[static_cast<std::size_t>(lod)];
    }
    raw_csv_ << '\n';
}

void BenchmarkRunner::record_quality(
    const ImageMetrics& image,
    const DepthMetrics& depth,
    const SilhouetteMetrics& silhouette,
    const EqualityMetrics& equality) {

    if (!active_) return;
    const auto& c = current_case();
    quality_csv_
        << case_index_ << ','
        << benchmark_study_name(c.study) << ','
        << benchmark_mode_name(c.mode) << ','
        << scene_name(c.scene) << ','
        << method_name(c.method) << ','
        << render_mode_name(c.benchmark_render_mode) << ','
        << shading_normal_mode_name(c.shading_normal_mode) << ','
        << current_camera_distance() << ','
        << c.camera_yaw << ','
        << c.camera_pitch << ','
        << c.camera_fov_degrees << ','
        << c.camera_target_x << ','
        << c.camera_target_z << ','
        << c.camera_target_height_offset << ','
        << c.error_budget_px << ','
        << c.normal_budget_degrees << ','
        << c.radiance_budget << ','
        << c.scientific_roughness << ','
        << c.light_azimuth_degrees << ','
        << c.light_elevation_degrees << ','
        << c.distance_lod_bias << ','
        << c.hysteresis << ','
        << c.morph_band << ','
        << c.width << ',' << c.height << ','
        << std::setprecision(10)
        << image.mae << ','
        << image.rmse << ','
        << image.relative_rmse << ','
        << image.foreground_rmse << ','
        << image.psnr << ','
        << image.max_abs_error << ','
        << image.p95_abs_error << ','
        << depth.mae << ','
        << depth.rmse << ','
        << depth.max_abs_error << ','
        << depth.p95_abs_error << ','
        << silhouette.coverage_mismatch_fraction << ','
        << silhouette.boundary_mean_distance_px << ','
        << silhouette.boundary_p95_distance_px << ','
        << silhouette.boundary_max_distance_px << ','
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
    if (config_.validation_suite) build_validation_cases();
    else if (config_.publication_quick) build_quick_cases();
    else if (config_.thesis_figure_suite) build_thesis_figure_cases();
    else if (config_.thesis_pbr_figure_suite) build_pbr_figure_cases();
    else if (config_.real_dem_validation_suite) build_real_dem_validation_cases();
    else build_full_cases();
}

void BenchmarkRunner::build_validation_cases() {
    const auto add = [&](BenchmarkCase c) { cases_.push_back(std::move(c)); };

    // Exact harness equivalence. These are hard gates.
    for (const SurfaceScene scene : {
             SurfaceScene::SmoothHills,
             SurfaceScene::SharpRidge,
             SurfaceScene::MixedFrequency,
             SurfaceScene::CliffBand}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::NullEquivalence;
        c.mode = BenchmarkMode::Null;
        c.scene = scene;
        c.method = ResearchMethod::ErrorBoundedFeatureAware;
        c.force_finest = true;
        c.width = 960;
        c.height = 540;
        add(c);
    }

    // CPU adaptive topology. No internal open or non-manifold edge is allowed.
    for (const SurfaceScene scene : {SurfaceScene::SharpRidge, SurfaceScene::GlacialValley}) {
        for (const float distance : {7.0f, 13.0f}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::HarnessValidation;
            c.scene = scene;
            c.method = ResearchMethod::AdaptiveErrorFeatureAware;
            c.camera_distance = distance;
            c.error_budget_px = 1.0f;
            c.hysteresis = 0.15f;
            c.morph_band = 0.75f;
            c.width = 960;
            c.height = 540;
            add(c);
        }
    }

    // Tightening a geometric pixel budget must not reduce generated geometry.
    for (const float budget : {0.5f, 1.0f, 2.0f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::HarnessValidation;
        c.scene = SurfaceScene::SharpRidge;
        c.method = ResearchMethod::TessellationError;
        c.camera_distance = 10.0f;
        c.error_budget_px = budget;
        c.width = 960;
        c.height = 540;
        add(c);
    }

    // A fixed pixel budget must not request less geometry at higher resolution.
    for (const auto& resolution : {std::pair{640, 360}, std::pair{1280, 720}, std::pair{1920, 1080}}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::HarnessValidation;
        c.scene = SurfaceScene::MixedFrequency;
        c.method = ResearchMethod::TessellationError;
        c.camera_distance = 11.0f;
        c.error_budget_px = 1.0f;
        c.width = resolution.first;
        c.height = resolution.second;
        add(c);
    }

    // A nearer camera must not request less geometry for the same budget.
    for (const float distance : {6.0f, 12.0f, 24.0f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::HarnessValidation;
        c.scene = SurfaceScene::MixedFrequency;
        c.method = ResearchMethod::TessellationError;
        c.camera_distance = distance;
        c.error_budget_px = 1.0f;
        c.width = 960;
        c.height = 540;
        add(c);
    }

    // Narrower FOV magnifies the same world-space residual and therefore cannot request less detail.
    for (const float fov : {35.0f, 55.0f, 80.0f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::FovScaling;
        c.scene = SurfaceScene::MixedFrequency;
        c.method = ResearchMethod::TessellationError;
        c.camera_distance = 12.0f;
        c.camera_fov_degrees = fov;
        c.error_budget_px = 1.0f;
        c.width = 960;
        c.height = 540;
        add(c);
    }

    // Height error is nearly view-aligned from above, so a top-down view should not refine
    // more than a grazing view of the same smooth heightfield at the same orbit radius.
    for (const float pitch : {0.12f, 1.35f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::ViewAngleScaling;
        c.scene = SurfaceScene::SmoothHills;
        c.method = ResearchMethod::TessellationError;
        c.camera_distance = 12.0f;
        c.camera_pitch = pitch;
        c.error_budget_px = 1.0f;
        c.width = 960;
        c.height = 540;
        add(c);
    }

    // Independent constraint monotonicity.
    for (const float normal_budget : {2.5f, 5.0f, 10.0f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::ConstraintIsolation;
        c.scene = SurfaceScene::SharpRidge;
        c.method = ResearchMethod::TessellationNormalBound;
        c.camera_distance = 10.0f;
        c.error_budget_px = 2.0f;
        c.normal_budget_degrees = normal_budget;
        c.width = 960;
        c.height = 540;
        add(c);
    }

    for (const float radiance_budget : {0.008f, 0.025f, 0.080f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::ConstraintIsolation;
        c.scene = SurfaceScene::SharpRidge;
        c.method = ResearchMethod::TessellationContextAware;
        c.camera_distance = 10.0f;
        c.camera_pitch = 0.24f;
        c.error_budget_px = 2.0f;
        c.normal_budget_degrees = 30.0f;
        c.radiance_budget = radiance_budget;
        c.scientific_roughness = 0.12f;
        c.light_azimuth_degrees = -65.0f;
        c.width = 960;
        c.height = 540;
        add(c);
    }

    // The context channel must collapse exactly when its extra constraints are disabled.
    for (const ResearchMethod method : {
             ResearchMethod::TessellationNormalBound,
             ResearchMethod::TessellationContextAware}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::ConstraintIsolation;
        c.scene = SurfaceScene::SharpRidge;
        c.method = method;
        c.camera_distance = 10.0f;
        c.error_budget_px = 1.0f;
        c.normal_budget_degrees = 5.0f;
        c.radiance_budget = 1000000.0f;
        c.width = 960;
        c.height = 540;
        add(c);
    }

    for (const ResearchMethod method : {
             ResearchMethod::TessellationError,
             ResearchMethod::TessellationContextAware}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::ConstraintIsolation;
        c.scene = SurfaceScene::MixedFrequency;
        c.method = method;
        c.camera_distance = 10.0f;
        c.error_budget_px = 1.0f;
        c.normal_budget_degrees = 1000000.0f;
        c.radiance_budget = 1000000.0f;
        c.width = 960;
        c.height = 540;
        add(c);
    }

    // Smoke-test every doctoral tessellation method on one heterogeneous scene.
    for (const ResearchMethod method : {
             ResearchMethod::TessellationDistance,
             ResearchMethod::TessellationVarianceBaseline,
             ResearchMethod::TessellationError,
             ResearchMethod::TessellationNormalBound,
             ResearchMethod::TessellationContextAware}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::HarnessValidation;
        c.scene = SurfaceScene::GlacialValley;
        c.method = method;
        c.camera_distance = 12.0f;
        c.error_budget_px = 1.0f;
        c.width = 960;
        c.height = 540;
        add(c);
    }
}

void BenchmarkRunner::build_quick_cases() {
    const auto add = [&](BenchmarkCase c) { cases_.push_back(std::move(c)); };

    // Compact calibration matrix. This deliberately produces several points per method,
    // allowing monotonicity and matched-quality analysis even in the GUI quick run.
    for (const SurfaceScene scene : {SurfaceScene::SharpRidge, SurfaceScene::MixedFrequency}) {
        for (const float distance : {8.0f, 16.0f}) {
            for (const float budget : {0.5f, 1.0f, 2.0f}) {
                for (const ResearchMethod method : {
                         ResearchMethod::TessellationError,
                         ResearchMethod::TessellationNormalBound,
                         ResearchMethod::TessellationContextAware}) {
                    BenchmarkCase c;
                    c.study = BenchmarkStudy::EstimatorCalibration;
                    c.scene = scene;
                    c.method = method;
                    c.camera_distance = distance;
                    c.error_budget_px = budget;
                    c.width = 1280;
                    c.height = 720;
                    add(c);
                }
            }
        }
    }

    // Strong-baseline sweep at one representative distance. Distance receives its own bias
    // sweep, while error-controlled methods receive a common pixel-budget sweep.
    for (const SurfaceScene scene : {SurfaceScene::SharpRidge, SurfaceScene::GlacialValley}) {
        for (const int bias : {-1, 0, 1}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::StrongBaseline;
            c.scene = scene;
            c.method = ResearchMethod::TessellationDistance;
            c.camera_distance = 11.0f;
            c.distance_lod_bias = bias;
            c.width = 1280;
            c.height = 720;
            add(c);
        }
        for (const float budget : {0.5f, 1.0f, 2.0f}) {
            for (const ResearchMethod method : {
                     ResearchMethod::TessellationVarianceBaseline,
                     ResearchMethod::TessellationError,
                     ResearchMethod::TessellationNormalBound,
                     ResearchMethod::TessellationContextAware}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::StrongBaseline;
                c.scene = scene;
                c.method = method;
                c.camera_distance = 11.0f;
                c.error_budget_px = budget;
                c.width = 1280;
                c.height = 720;
                c.capture_images = budget == 1.0f;
                add(c);
            }
        }
    }

    // Isolate the radiance channel. The normal constraint is deliberately relaxed so that
    // the context term can either earn its extra geometry or collapse to the cheaper state.
    for (const float roughness : {0.12f, 0.45f, 0.85f}) {
        for (const float light_azimuth : {-65.0f, 35.0f}) {
            for (const ResearchMethod method : {
                     ResearchMethod::TessellationError,
                     ResearchMethod::TessellationNormalBound}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::ContextSensitivity;
                c.scene = SurfaceScene::SharpRidge;
                c.method = method;
                c.camera_distance = 9.0f;
                c.camera_pitch = 0.24f;
                c.normal_budget_degrees = 20.0f;
                c.scientific_roughness = roughness;
                c.light_azimuth_degrees = light_azimuth;
                c.width = 1280;
                c.height = 720;
                add(c);
            }
            for (const float radiance_budget : {0.010f, 0.040f}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::ContextSensitivity;
                c.scene = SurfaceScene::SharpRidge;
                c.method = ResearchMethod::TessellationContextAware;
                c.camera_distance = 9.0f;
                c.camera_pitch = 0.24f;
                c.normal_budget_degrees = 20.0f;
                c.radiance_budget = radiance_budget;
                c.scientific_roughness = roughness;
                c.light_azimuth_degrees = light_azimuth;
                c.width = 1280;
                c.height = 720;
                add(c);
            }
        }
    }

    for (const SurfaceScene scene : {SurfaceScene::SharpRidge, SurfaceScene::AlpineEscarpment}) {
        for (const float pitch : {0.10f, 0.48f}) {
            for (const ResearchMethod method : {
                     ResearchMethod::TessellationError,
                     ResearchMethod::TessellationNormalBound,
                     ResearchMethod::TessellationContextAware}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::SilhouetteStress;
                c.scene = scene;
                c.method = method;
                c.camera_distance = 10.0f;
                c.camera_pitch = pitch;
                c.width = 1280;
                c.height = 720;
                add(c);
            }
        }
    }

    for (const float pitch : {0.12f, 0.52f, 1.25f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::ViewAngleScaling;
        c.scene = SurfaceScene::MixedFrequency;
        c.method = ResearchMethod::TessellationError;
        c.camera_distance = 12.0f;
        c.camera_pitch = pitch;
        c.width = 1280;
        c.height = 720;
        add(c);
    }

    for (const float fov : {35.0f, 55.0f, 80.0f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::FovScaling;
        c.scene = SurfaceScene::MixedFrequency;
        c.method = ResearchMethod::TessellationError;
        c.camera_distance = 12.0f;
        c.camera_fov_degrees = fov;
        c.width = 1280;
        c.height = 720;
        add(c);
    }

    for (const SurfaceScene scene : {SurfaceScene::SmoothHills, SurfaceScene::Badlands}) {
        for (const ResearchMethod method : {
                 ResearchMethod::TessellationDistance,
                 ResearchMethod::TessellationVarianceBaseline,
                 ResearchMethod::TessellationError,
                 ResearchMethod::TessellationNormalBound,
                 ResearchMethod::TessellationContextAware}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::NegativeControl;
            c.scene = scene;
            c.method = method;
            c.camera_distance = 12.0f;
            c.error_budget_px = 1.0f;
            c.width = 1280;
            c.height = 720;
            add(c);
        }
    }

    for (const float hysteresis : {0.0f, 0.15f, 0.30f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::TemporalStability;
        c.mode = BenchmarkMode::Temporal;
        c.scene = SurfaceScene::GlacialValley;
        c.method = ResearchMethod::AdaptiveErrorFeatureAware;
        c.camera_distance = 10.0f;
        c.camera_motion_amplitude = 2.8f;
        c.camera_motion_cycles = 3.0f;
        c.hysteresis = hysteresis;
        c.width = 1280;
        c.height = 720;
        add(c);
    }

    for (const auto scene : {
             SurfaceScene::SmoothHills,
             SurfaceScene::SharpRidge,
             SurfaceScene::MixedFrequency,
             SurfaceScene::CliffBand}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::NullEquivalence;
        c.mode = BenchmarkMode::Null;
        c.scene = scene;
        c.method = ResearchMethod::ErrorBoundedFeatureAware;
        c.force_finest = true;
        c.width = 1280;
        c.height = 720;
        c.capture_images = true;
        add(c);
    }
}

void BenchmarkRunner::build_thesis_figure_cases() {
    constexpr int capture_width = 2560;
    constexpr int capture_height = 1440;


    const auto add_capture = [&](BenchmarkCase c, bool diagnostics) {
        c.study = BenchmarkStudy::ThesisFigure;
        c.width = capture_width;
        c.height = capture_height;
        c.capture_images = true;
        c.capture_diagnostics = diagnostics;
        cases_.push_back(std::move(c));
    };

    // Clean raw overview captures. These are independent of the stress-test cameras and
    // are the only captures used by the thesis scene gallery.
    for (const SurfaceScene scene : {
             SurfaceScene::SmoothHills,
             SurfaceScene::SharpRidge,
             SurfaceScene::MixedFrequency,
             SurfaceScene::CliffBand,
             SurfaceScene::AlpineEscarpment,
             SurfaceScene::GlacialValley,
             SurfaceScene::Badlands,
             SurfaceScene::FracturedHighlands}) {
        BenchmarkCase c;
        c.scene = scene;
        c.method = ResearchMethod::TessellationError;
        c.error_budget_px = scene == SurfaceScene::GlacialValley ? 0.7f : 1.0f;
        apply_overview_camera(c, scene);
        add_capture(c, scene == SurfaceScene::GlacialValley);
    }

    // Raw camera-selection sweep for the Alpine Escarpment. These captures are not used
    // as performance evidence; they allow the final dissertation figure to be selected from
    // real renderer output without another code change or any synthetic/retouched image.
    for (const float yaw : {2.06f, 2.32f, 2.58f, 2.84f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::ThesisCamera;
        c.scene = SurfaceScene::AlpineEscarpment;
        c.method = ResearchMethod::TessellationError;
        c.error_budget_px = 1.0f;
        apply_overview_camera(c, SurfaceScene::AlpineEscarpment);
        c.camera_yaw = yaw;
        c.capture_images = true;
        c.capture_diagnostics = false;
        c.width = capture_width;
        c.height = capture_height;
        cases_.push_back(std::move(c));
    }

    // Matching camera sweep for the analytic cliff-band control, again using raw renderer output
    // only. The goal is to ensure that the visible step is presented clearly in the final thesis.
    for (const float yaw : {1.92f, 2.18f, 2.44f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::ThesisCamera;
        c.scene = SurfaceScene::CliffBand;
        c.method = ResearchMethod::TessellationError;
        c.error_budget_px = 1.0f;
        apply_overview_camera(c, SurfaceScene::CliffBand);
        c.camera_yaw = yaw;
        c.capture_images = true;
        c.capture_diagnostics = false;
        c.width = capture_width;
        c.height = capture_height;
        cases_.push_back(std::move(c));
    }

    // Matched method family on one heterogeneous scene. All methods use exactly the same
    // camera, light and scientific shading, so reference/proposed screenshots are directly
    // comparable in color as well as geometry.
    for (const ResearchMethod method : {
             ResearchMethod::TessellationDistance,
             ResearchMethod::TessellationVarianceBaseline,
             ResearchMethod::TessellationError,
             ResearchMethod::TessellationNormalBound,
             ResearchMethod::TessellationContextAware}) {
        BenchmarkCase c;
        c.scene = SurfaceScene::GlacialValley;
        c.method = method;
        c.error_budget_px = 0.7f;
        apply_overview_camera(c, SurfaceScene::GlacialValley);
        add_capture(c, method == ResearchMethod::TessellationError);
    }

    // Shading-normal sensitivity: compare faceted versus smooth normals under exactly the
    // same geometry selection. Scientific mode measures controller quality without materials,
    // and realistic mode shows the presentation-side effect under PBR terrain shading.
    for (const SurfaceScene scene : {SurfaceScene::SmoothHills, SurfaceScene::SharpRidge, SurfaceScene::MixedFrequency, SurfaceScene::CliffBand, SurfaceScene::ErodedMountain, SurfaceScene::PlateauCanyon, SurfaceScene::AlpineEscarpment, SurfaceScene::GlacialValley, SurfaceScene::RiverBasin, SurfaceScene::Badlands, SurfaceScene::FracturedHighlands}) {
        for (const RenderMode mode : {RenderMode::Scientific, RenderMode::Realistic}) {
            for (const ShadingNormalMode normal_mode : {ShadingNormalMode::Geometric, ShadingNormalMode::Smooth}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::ShadingNormalStudy;
                c.scene = scene;
                c.method = ResearchMethod::TessellationError;
                c.benchmark_render_mode = mode;
                c.shading_normal_mode = normal_mode;
                c.error_budget_px = scene == SurfaceScene::GlacialValley ? 0.7f : 1.0f;
                apply_overview_camera(c, scene);
                c.width = capture_width;
                c.height = capture_height;
                c.capture_images = true;
                c.capture_diagnostics = mode == RenderMode::Realistic && normal_mode == ShadingNormalMode::Smooth;
                cases_.push_back(std::move(c));
            }
        }
    }

    // Silhouette stress stays deliberately low-angle, but it is no longer reused as a
    // general scene illustration.
    for (const float pitch : {0.10f, 0.34f}) {
        for (const ResearchMethod method : {
                 ResearchMethod::TessellationError,
                 ResearchMethod::TessellationNormalBound,
                 ResearchMethod::TessellationContextAware}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::SilhouetteStress;
            c.scene = SurfaceScene::SharpRidge;
            c.method = method;
            c.camera_distance = 13.5f;
            c.camera_yaw = 0.58f;
            c.camera_pitch = pitch;
            c.camera_fov_degrees = 50.0f;
            c.camera_target_height_offset = 0.08f;
            c.error_budget_px = 1.0f;
            c.width = capture_width;
            c.height = capture_height;
            c.capture_images = true;
            c.capture_diagnostics = false;
            cases_.push_back(std::move(c));
        }
    }

    // Controlled PBR material sensitivity at a single fixed overview camera.
    for (const float roughness : {0.12f, 0.45f, 0.85f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::ContextSensitivity;
        c.scene = SurfaceScene::GlacialValley;
        c.method = ResearchMethod::TessellationContextAware;
        apply_overview_camera(c, SurfaceScene::GlacialValley);
        c.error_budget_px = 0.7f;
        c.normal_budget_degrees = 20.0f;
        c.radiance_budget = 0.020f;
        c.scientific_roughness = roughness;
        c.light_azimuth_degrees = -25.0f;
        c.width = capture_width;
        c.height = capture_height;
        c.capture_images = true;
        c.capture_diagnostics = true;
        cases_.push_back(std::move(c));
    }

    // Negative controls use the same publication camera policy rather than arbitrary
    // stress views. They remain separate studies in CSV output.
    for (const SurfaceScene scene : {SurfaceScene::SmoothHills, SurfaceScene::Badlands}) {
        for (const ResearchMethod method : {
                 ResearchMethod::TessellationVarianceBaseline,
                 ResearchMethod::TessellationError,
                 ResearchMethod::TessellationContextAware}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::NegativeControl;
            c.scene = scene;
            c.method = method;
            apply_overview_camera(c, scene);
            c.error_budget_px = 1.0f;
            c.width = capture_width;
            c.height = capture_height;
            c.capture_images = true;
            c.capture_diagnostics = false;
            cases_.push_back(std::move(c));
        }
    }
}

void BenchmarkRunner::build_pbr_figure_cases() {
    constexpr int capture_width = 2560;
    constexpr int capture_height = 1440;

    for (const SurfaceScene scene : {
             SurfaceScene::SmoothHills,
             SurfaceScene::SharpRidge,
             SurfaceScene::MixedFrequency,
             SurfaceScene::CliffBand,
             SurfaceScene::ErodedMountain,
             SurfaceScene::PlateauCanyon,
             SurfaceScene::AlpineEscarpment,
             SurfaceScene::GlacialValley,
             SurfaceScene::RiverBasin,
             SurfaceScene::Badlands,
             SurfaceScene::FracturedHighlands}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::PbrFigure;
        c.scene = scene;
        c.method = ResearchMethod::TessellationContextAware;
        c.benchmark_render_mode = RenderMode::Realistic;
        c.shading_normal_mode = ShadingNormalMode::Smooth;
        c.error_budget_px = scene == SurfaceScene::GlacialValley ? 0.7f : 1.0f;
        c.normal_budget_degrees = 5.0f;
        c.radiance_budget = 0.025f;
        apply_overview_camera(c, scene);
        c.width = capture_width;
        c.height = capture_height;
        c.capture_images = true;
        c.capture_diagnostics = true;
        cases_.push_back(std::move(c));
    }

    for (const float yaw : {1.92f, 2.18f, 2.44f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::PbrFigure;
        c.scene = SurfaceScene::CliffBand;
        c.method = ResearchMethod::TessellationContextAware;
        c.benchmark_render_mode = RenderMode::Realistic;
        c.shading_normal_mode = ShadingNormalMode::Smooth;
        c.error_budget_px = 1.0f;
        c.normal_budget_degrees = 5.0f;
        c.radiance_budget = 0.025f;
        apply_overview_camera(c, SurfaceScene::CliffBand);
        c.camera_yaw = yaw;
        c.width = capture_width;
        c.height = capture_height;
        c.capture_images = true;
        c.capture_diagnostics = true;
        cases_.push_back(std::move(c));
    }

    for (const float yaw : {2.06f, 2.32f, 2.58f, 2.84f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::PbrFigure;
        c.scene = SurfaceScene::AlpineEscarpment;
        c.method = ResearchMethod::TessellationContextAware;
        c.benchmark_render_mode = RenderMode::Realistic;
        c.shading_normal_mode = ShadingNormalMode::Smooth;
        c.error_budget_px = 1.0f;
        c.normal_budget_degrees = 5.0f;
        c.radiance_budget = 0.025f;
        apply_overview_camera(c, SurfaceScene::AlpineEscarpment);
        c.camera_yaw = yaw;
        c.width = capture_width;
        c.height = capture_height;
        c.capture_images = true;
        c.capture_diagnostics = true;
        cases_.push_back(std::move(c));
    }
}


void BenchmarkRunner::build_full_cases() {
    const auto add = [&](BenchmarkCase c) { cases_.push_back(std::move(c)); };
    const SurfaceScene analytic_scenes[] = {
        SurfaceScene::SmoothHills,
        SurfaceScene::SharpRidge,
        SurfaceScene::MixedFrequency,
        SurfaceScene::CliffBand
    };
    const SurfaceScene natural_scenes[] = {
        SurfaceScene::ErodedMountain,
        SurfaceScene::AlpineEscarpment,
        SurfaceScene::GlacialValley,
        SurfaceScene::RiverBasin,
        SurfaceScene::Badlands,
        SurfaceScene::FracturedHighlands
    };

    // Stage A: calibration on controlled analytic scenes. These cases test whether tightening
    // an explicit budget produces a predictable change in measured image/depth/silhouette error.
    for (const SurfaceScene scene : analytic_scenes) {
        for (const float distance : {7.0f, 13.0f, 22.0f}) {
            for (const float budget : {0.25f, 0.5f, 1.0f, 2.0f}) {
                for (const ResearchMethod method : {
                         ResearchMethod::TessellationError,
                         ResearchMethod::TessellationNormalBound,
                         ResearchMethod::TessellationContextAware}) {
                    BenchmarkCase c;
                    c.study = BenchmarkStudy::EstimatorCalibration;
                    c.scene = scene;
                    c.method = method;
                    c.camera_distance = distance;
                    c.error_budget_px = budget;
                    c.width = 1920;
                    c.height = 1080;
                    c.capture_images = budget == 1.0f && distance == 13.0f;
                    add(c);
                }
            }
        }
    }

    // Stage B: strong baselines on heterogeneous, terrain-like scenes. Distance gets its own
    // sweep instead of one hand-picked threshold. Variance is a deliberately strong terrain
    // roughness baseline, not a strawman.
    for (const SurfaceScene scene : natural_scenes) {
        for (const float distance : {8.0f, 15.0f, 24.0f}) {
            for (const int bias : {-1, 0, 1}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::StrongBaseline;
                c.scene = scene;
                c.method = ResearchMethod::TessellationDistance;
                c.camera_distance = distance;
                c.distance_lod_bias = bias;
                add(c);
            }
            for (const float budget : {0.35f, 0.7f, 1.4f, 2.8f}) {
                for (const ResearchMethod method : {
                         ResearchMethod::TessellationVarianceBaseline,
                         ResearchMethod::TessellationError,
                         ResearchMethod::TessellationNormalBound,
                         ResearchMethod::TessellationContextAware}) {
                    BenchmarkCase c;
                    c.study = BenchmarkStudy::StrongBaseline;
                    c.scene = scene;
                    c.method = method;
                    c.camera_distance = distance;
                    c.error_budget_px = budget;
                    c.capture_images = scene == SurfaceScene::GlacialValley
                        && distance == 15.0f && budget == 0.7f;
                    add(c);
                }
            }
        }
    }

    // Stage C: isolate V/L/M dependence. The normal constraint is relaxed in this study,
    // otherwise a strict angular bound can dominate and make the radiance channel unidentifiable.
    for (const SurfaceScene scene : {SurfaceScene::SharpRidge, SurfaceScene::GlacialValley}) {
        for (const float roughness : {0.12f, 0.40f, 0.85f}) {
            for (const float light_azimuth : {-70.0f, 0.0f, 70.0f}) {
                for (const ResearchMethod method : {
                         ResearchMethod::TessellationError,
                         ResearchMethod::TessellationNormalBound}) {
                    BenchmarkCase c;
                    c.study = BenchmarkStudy::ContextSensitivity;
                    c.scene = scene;
                    c.method = method;
                    c.camera_distance = 10.0f;
                    c.camera_pitch = 0.28f;
                    c.error_budget_px = 1.0f;
                    c.normal_budget_degrees = 20.0f;
                    c.scientific_roughness = roughness;
                    c.light_azimuth_degrees = light_azimuth;
                    add(c);
                }
                for (const float radiance_budget : {0.008f, 0.020f, 0.050f}) {
                    BenchmarkCase c;
                    c.study = BenchmarkStudy::ContextSensitivity;
                    c.scene = scene;
                    c.method = ResearchMethod::TessellationContextAware;
                    c.camera_distance = 10.0f;
                    c.camera_pitch = 0.28f;
                    c.error_budget_px = 1.0f;
                    c.normal_budget_degrees = 20.0f;
                    c.radiance_budget = radiance_budget;
                    c.scientific_roughness = roughness;
                    c.light_azimuth_degrees = light_azimuth;
                    c.capture_images = roughness == 0.12f
                        && light_azimuth == 0.0f && radiance_budget == 0.020f;
                    add(c);
                }
            }
        }
    }

    // Stage C2: shading-normal sensitivity. These are formal quality experiments rather than
    // illustration-only captures. They separate geometric adaptation from the presentation-side
    // effect of faceted versus smooth shading normals, both in the scientific view and in the
    // realistic PBR view.
    for (const SurfaceScene scene : {SurfaceScene::SmoothHills, SurfaceScene::SharpRidge, SurfaceScene::MixedFrequency, SurfaceScene::CliffBand, SurfaceScene::ErodedMountain, SurfaceScene::PlateauCanyon, SurfaceScene::AlpineEscarpment, SurfaceScene::GlacialValley, SurfaceScene::RiverBasin, SurfaceScene::Badlands, SurfaceScene::FracturedHighlands}) {
        for (const RenderMode mode : {RenderMode::Scientific, RenderMode::Realistic}) {
            for (const ShadingNormalMode normal_mode : {ShadingNormalMode::Geometric, ShadingNormalMode::Smooth}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::ShadingNormalStudy;
                c.scene = scene;
                c.method = ResearchMethod::TessellationError;
                c.benchmark_render_mode = mode;
                c.shading_normal_mode = normal_mode;
                c.camera_distance = scene == SurfaceScene::GlacialValley ? 15.0f : 12.0f;
                c.error_budget_px = scene == SurfaceScene::GlacialValley ? 0.7f : 1.0f;
                apply_overview_camera(c, scene);
                c.capture_images = true;
                c.capture_diagnostics = mode == RenderMode::Realistic && normal_mode == ShadingNormalMode::Smooth;
                add(c);
            }
        }
    }

    // Stage D: silhouette/coverage is a qualitatively different error channel. These cases
    // deliberately lower the camera to make a one-pixel contour shift visible in the metrics.
    for (const SurfaceScene scene : {
             SurfaceScene::SharpRidge,
             SurfaceScene::AlpineEscarpment,
             SurfaceScene::GlacialValley}) {
        for (const float pitch : {0.08f, 0.14f, 0.26f, 0.52f}) {
            for (const ResearchMethod method : {
                     ResearchMethod::TessellationVarianceBaseline,
                     ResearchMethod::TessellationError,
                     ResearchMethod::TessellationNormalBound,
                     ResearchMethod::TessellationContextAware}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::SilhouetteStress;
                c.scene = scene;
                c.method = method;
                c.camera_distance = 11.0f;
                c.camera_yaw = 0.58f;
                c.camera_pitch = pitch;
                c.error_budget_px = 1.0f;
                c.capture_images = pitch == 0.08f;
                add(c);
            }
        }
    }

    // Stage E: resolution law. The same pixel-domain budget should force more detail as the
    // target resolution grows, while the measured silhouette error remains in a comparable range.
    for (const auto& resolution : {
             std::pair{1280, 720},
             std::pair{1920, 1080},
             std::pair{3840, 2160}}) {
        for (const ResearchMethod method : {
                 ResearchMethod::TessellationError,
                 ResearchMethod::TessellationContextAware}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::ResolutionScaling;
            c.scene = SurfaceScene::GlacialValley;
            c.method = method;
            c.camera_distance = 12.0f;
            c.width = resolution.first;
            c.height = resolution.second;
            add(c);
        }
    }

    // Stage E2: explicit projection laws. These are measured studies, not only unit tests.
    for (const SurfaceScene scene : {SurfaceScene::SmoothHills, SurfaceScene::MixedFrequency}) {
        for (const float pitch : {0.10f, 0.28f, 0.52f, 1.10f, 1.35f}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::ViewAngleScaling;
            c.scene = scene;
            c.method = ResearchMethod::TessellationError;
            c.camera_distance = 12.0f;
            c.camera_pitch = pitch;
            c.error_budget_px = 1.0f;
            add(c);
        }
    }

    for (const float fov : {35.0f, 45.0f, 55.0f, 70.0f, 80.0f}) {
        for (const ResearchMethod method : {
                 ResearchMethod::TessellationError,
                 ResearchMethod::TessellationContextAware}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::FovScaling;
            c.scene = SurfaceScene::MixedFrequency;
            c.method = method;
            c.camera_distance = 12.0f;
            c.camera_fov_degrees = fov;
            c.normal_budget_degrees = 20.0f;
            add(c);
        }
    }

    // Stage E3: isolate individual constraint channels. These sweeps answer whether the
    // multi-channel controller behaves as a compositional contract rather than an opaque heuristic.
    for (const float normal_budget : {2.5f, 5.0f, 10.0f, 20.0f, 40.0f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::ConstraintIsolation;
        c.scene = SurfaceScene::SharpRidge;
        c.method = ResearchMethod::TessellationNormalBound;
        c.camera_distance = 10.0f;
        c.error_budget_px = 2.0f;
        c.normal_budget_degrees = normal_budget;
        add(c);
    }
    for (const float roughness : {0.12f, 0.45f, 0.85f}) {
        for (const float radiance_budget : {0.005f, 0.010f, 0.020f, 0.050f, 0.100f}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::ConstraintIsolation;
            c.scene = SurfaceScene::SharpRidge;
            c.method = ResearchMethod::TessellationContextAware;
            c.camera_distance = 10.0f;
            c.camera_pitch = 0.24f;
            c.error_budget_px = 2.0f;
            c.normal_budget_degrees = 30.0f;
            c.radiance_budget = radiance_budget;
            c.scientific_roughness = roughness;
            c.light_azimuth_degrees = -65.0f;
            add(c);
        }
    }

    // Stage F: negative controls. Smooth hills expose over-conservatism. Badlands expose the
    // case where difficult detail is dense enough that little geometry can be saved.
    for (const SurfaceScene scene : {SurfaceScene::SmoothHills, SurfaceScene::Badlands}) {
        for (const ResearchMethod method : {
                 ResearchMethod::TessellationDistance,
                 ResearchMethod::TessellationVarianceBaseline,
                 ResearchMethod::TessellationError,
                 ResearchMethod::TessellationNormalBound,
                 ResearchMethod::TessellationContextAware}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::NegativeControl;
            c.scene = scene;
            c.method = method;
            c.camera_distance = 12.0f;
            c.error_budget_px = 1.0f;
            c.capture_images = true;
            add(c);
        }
    }

    // Stage G: retain the CPU hierarchy temporal experiment because it has state and hysteresis.
    // GPU fractional tessellation is stateless in this revision and therefore is not used to make
    // a hysteresis claim.
    for (const float hysteresis : {0.0f, 0.10f, 0.20f, 0.30f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::TemporalStability;
        c.mode = BenchmarkMode::Temporal;
        c.scene = SurfaceScene::GlacialValley;
        c.method = ResearchMethod::AdaptiveErrorFeatureAware;
        c.camera_distance = 10.0f;
        c.camera_motion_amplitude = 2.8f;
        c.camera_motion_cycles = 4.0f;
        c.error_budget_px = 1.0f;
        c.hysteresis = hysteresis;
        c.morph_band = 0.75f;
        add(c);
    }

    // Stage H: byte-exact null test remains on the same patch submission path.
    for (const SurfaceScene scene : analytic_scenes) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::NullEquivalence;
        c.mode = BenchmarkMode::Null;
        c.scene = scene;
        c.method = ResearchMethod::ErrorBoundedFeatureAware;
        c.force_finest = true;
        c.capture_images = true;
        add(c);
    }
}

void BenchmarkRunner::build_real_dem_validation_cases() {
    const auto add = [&](BenchmarkCase c) { cases_.push_back(std::move(c)); };
    constexpr SurfaceScene scene = SurfaceScene::MountStHelensDem;
    constexpr std::array<float, 3> distances{8.0f, 13.0f, 20.0f};
    constexpr std::array<float, 6> budgets{0.35f, 0.50f, 0.70f, 1.00f, 1.40f, 2.00f};

    const auto configure_camera = [](BenchmarkCase& c) {
        c.camera_yaw = 2.34f;
        c.camera_pitch = 0.48f;
        c.camera_fov_degrees = 52.0f;
        c.camera_target_x = 0.0f;
        c.camera_target_z = 0.0f;
        c.camera_target_height_offset = 0.10f;
        c.width = 1920;
        c.height = 1080;
    };

    // First case is deliberately a visible smoke test. If loading/rendering the measured DEM
    // succeeds, a raw PBR capture exists immediately instead of only near the end of the suite.
    {
        BenchmarkCase c;
        c.study = BenchmarkStudy::RealDemValidation;
        c.scene = scene;
        c.method = ResearchMethod::TessellationError;
        c.benchmark_render_mode = RenderMode::Scientific;
        c.shading_normal_mode = ShadingNormalMode::Geometric;
        c.camera_distance = 17.3f;
        c.error_budget_px = 0.70f;
        configure_camera(c);
        c.width = 1280;
        c.height = 720;
        c.capture_images = true;
        add(c);
    }

    // External-validity core: compare the two local reconstruction-error signals on the
    // measured USGS heightfield without changing the preregistered 688-case core matrix.
    for (const ResearchMethod method : {
             ResearchMethod::TessellationVarianceBaseline,
             ResearchMethod::TessellationError}) {
        for (const float distance : distances) {
            for (const float budget : budgets) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::RealDemValidation;
                c.scene = scene;
                c.method = method;
                c.benchmark_render_mode = RenderMode::Scientific;
                c.shading_normal_mode = ShadingNormalMode::Geometric;
                c.camera_distance = distance;
                c.error_budget_px = budget;
                configure_camera(c);
                add(c);
            }
        }
    }

    // Practical distance-only baseline receives its own bias sweep instead of one hand-picked point.
    for (const float distance : distances) {
        for (const int bias : {-2, -1, 0, 1, 2}) {
            BenchmarkCase c;
            c.study = BenchmarkStudy::RealDemValidation;
            c.scene = scene;
            c.method = ResearchMethod::TessellationDistance;
            c.benchmark_render_mode = RenderMode::Scientific;
            c.shading_normal_mode = ShadingNormalMode::Geometric;
            c.camera_distance = distance;
            c.distance_lod_bias = bias;
            configure_camera(c);
            add(c);
        }
    }

    // A small supplementary probe checks whether normal/radiometric constraints behave similarly
    // on measured terrain. These cases are not used to redefine the primary external-validation claim.
    for (const ResearchMethod method : {
             ResearchMethod::TessellationNormalBound,
             ResearchMethod::TessellationContextAware}) {
        for (const float distance : distances) {
            for (const float budget : {0.70f, 1.40f}) {
                BenchmarkCase c;
                c.study = BenchmarkStudy::RealDemValidation;
                c.scene = scene;
                c.method = method;
                c.benchmark_render_mode = RenderMode::Scientific;
                c.shading_normal_mode = ShadingNormalMode::Geometric;
                c.camera_distance = distance;
                c.error_budget_px = budget;
                c.normal_budget_degrees = 5.0f;
                c.radiance_budget = 0.025f;
                configure_camera(c);
                add(c);
            }
        }
    }

    // Thesis-camera sweep around the complete dense mountain heightfield.
    // These cases are supplementary image captures and are excluded from primary statistics.
    for (const float yaw : {1.55f, 2.05f, 2.34f, 2.75f}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::RealDemValidation;
        c.scene = scene;
        c.method = ResearchMethod::TessellationError;
        c.benchmark_render_mode = RenderMode::Realistic;
        c.shading_normal_mode = ShadingNormalMode::Smooth;
        c.camera_distance = 17.3f;
        c.error_budget_px = 0.70f;
        configure_camera(c);
        c.camera_yaw = yaw;
        c.width = 1920;
        c.height = 1080;
        c.capture_images = true;
        add(c);
    }

    // Null-equivalence gate on the measured asset.
    {
        BenchmarkCase c;
        c.study = BenchmarkStudy::RealDemValidation;
        c.mode = BenchmarkMode::Null;
        c.scene = scene;
        c.method = ResearchMethod::TessellationError;
        c.benchmark_render_mode = RenderMode::Scientific;
        c.shading_normal_mode = ShadingNormalMode::Geometric;
        c.camera_distance = 13.0f;
        c.force_finest = true;
        c.capture_images = true;
        configure_camera(c);
        add(c);
    }

    // Two raw PBR captures use the same measured geometry and frozen camera for thesis figures.
    for (const ResearchMethod method : {
             ResearchMethod::TessellationVarianceBaseline,
             ResearchMethod::TessellationError}) {
        BenchmarkCase c;
        c.study = BenchmarkStudy::RealDemValidation;
        c.scene = scene;
        c.method = method;
        c.benchmark_render_mode = RenderMode::Realistic;
        c.shading_normal_mode = ShadingNormalMode::Smooth;
        c.camera_distance = 18.0f;
        c.error_budget_px = 0.70f;
        apply_overview_camera(c, scene);
        c.width = 2560;
        c.height = 1440;
        c.capture_images = true;
        c.capture_diagnostics = method == ResearchMethod::TessellationError;
        add(c);
    }
}

void BenchmarkRunner::write_manifest(const RuntimeInfo& runtime_info) const {
    std::ofstream out(config_.output_dir / "manifest.txt");
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    out << "experiment=gfx-research-terrain-doctoral-v6.11\n";
    out << "thesis_capture_revision=v6.11\n";
    out << "validation_suite=" << (config_.validation_suite ? 1 : 0) << '\n';
    out << "real_dem_validation_suite=" << (config_.real_dem_validation_suite ? 1 : 0) << '\n';
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
    out << "adaptive_max_level=" << config_.adaptive_max_level << '\n';
    out << "tessellation_patch_grid=" << config_.tessellation_patch_grid << '\n';
    out << "tessellation_max_level=" << config_.tessellation_max_level << '\n';
    out << "world_size=" << config_.world_size << '\n';
    out << "default_normal_budget_degrees=" << config_.normal_budget_degrees << '\n';
    out << "default_radiance_budget=" << config_.radiance_budget << '\n';
    out << "default_scientific_roughness=" << config_.scientific_roughness << '\n';
    out << "default_light_azimuth_degrees=" << config_.light_azimuth_degrees << '\n';
    out << "default_light_elevation_degrees=" << config_.light_elevation_degrees << '\n';
    out << "warmup_frames=" << warmup_frames_ << '\n';
    out << "sample_frames=" << sample_frames_ << '\n';
    out << "case_count=" << cases_.size() << '\n';
    if (config_.real_dem_validation_suite) {
        out << "real_dem_dataset=USGS 3DEP 1/3 arc-second DEM; Mount St. Helens summit-centered crop\n";
        out << "real_dem_source_catalog=https://data.usgs.gov/datacatalog/data/USGS:3a81321b-c153-416f-98b7-cc8e5f0e17c3\n";
        out << "real_dem_source_tile=USGS_13_n47w123_20250813.tif\n";
        out << "real_dem_source_sha256=ab4fb0a2afa65c49c41cb0c59c5ea102329b6e1e47425159b884d05f540c4bb5\n";
        out << "real_dem_source_resolution=1/3 arc-second (approximately 10 m)\n";
        out << "real_dem_source_crs=EPSG:4269 (NAD83 geographic)\n";
        out << "real_dem_working_grid=1921x1921 samples in EPSG:26910 at 10 m spacing\n";
        out << "real_dem_crop_span_m=19200\n";
        out << "real_dem_center=46.1914,-122.1956\n";
        out << "real_dem_world_mapping=metric scale preserved in a 19.2 km square; constant vertical offset only\n";
        out << "real_dem_nodata_handling=none inside the prepared crop; setup script rejects any non-finite output sample\n";
        out << "real_dem_preprocessing=bilinear reprojection from USGS 3DEP to a regular NAD83 / UTM zone 10N 10 m grid; no hole filling or synthetic extrapolation\n";
        out << "real_dem_runtime_loader=validated packaged canonical RAW only; GeoTIFF preprocessing is offline-only\n";
        out << "real_dem_runtime_grid=1921x1921 little-endian float32; exact-size and finite-sample validation before GPU upload\n";
        out << "real_dem_provenance_file=assets/dem/mount_st_helens_usgs_10m_1921_metadata.json\n";
    }
    out << "scientific_material=fixed albedo plus controllable GGX roughness; external textures disabled\n";
    out << "shading_normal_modes=auto(scientific=geometric, realistic=smooth), geometric, smooth\n";
    out << "context_estimator=finite-difference local shading sensitivity plus explicit geometric constraint\n";
    out << "silhouette_metric=symmetric 8-neighbor chamfer boundary distance in pixels\n";
    out << "reference=finest available patch mesh at every patch\n";
    out << "timing=asynchronous GPU timestamp queries from gfx-research-base\n";
}

void BenchmarkRunner::ensure_csv_open() {
    raw_csv_.open(config_.output_dir / "raw.csv", std::ios::trunc);
    quality_csv_.open(config_.output_dir / "quality.csv", std::ios::trunc);

    raw_csv_
        << "case,study,mode,scene,method,render_mode,shading_normal_mode,camera_distance,base_distance,camera_yaw,camera_pitch,"
           "camera_fov_degrees,camera_target_x,camera_target_z,camera_target_height_offset,"
           "motion_amplitude,error_budget_px,"
           "normal_budget_degrees,radiance_budget,scientific_roughness,light_azimuth_degrees,"
           "light_elevation_degrees,distance_lod_bias,hysteresis,morph_band,width,height,frame,gpu_ms,"
           "triangles,lod_transitions,max_geometric_error_px,max_controller_error_px,mean_geometric_error_px,"
           "adaptive_leaf_count,morphed_vertices,saturated_regions,mean_refinement_level,adaptation_cpu_ms,"
           "topology_open_internal_edges,topology_nonmanifold_edges";
    for (int lod = 0; lod < max_lod_count; ++lod) raw_csv_ << ",lod" << lod << "_patches";
    raw_csv_ << '\n';

    quality_csv_
        << "case,study,mode,scene,method,render_mode,shading_normal_mode,camera_distance,camera_yaw,camera_pitch,camera_fov_degrees,camera_target_x,"
           "camera_target_z,camera_target_height_offset,error_budget_px,normal_budget_degrees,radiance_budget,"
           "scientific_roughness,light_azimuth_degrees,light_elevation_degrees,distance_lod_bias,hysteresis,"
           "morph_band,width,height,image_mae,image_rmse,image_relative_rmse,image_foreground_rmse,image_psnr,"
           "image_max_abs,image_p95_abs,depth_mae,depth_rmse,depth_max_abs,depth_p95_abs,"
           "coverage_mismatch_fraction,boundary_mean_distance_px,boundary_p95_distance_px,"
           "boundary_max_distance_px,rgba_exact,depth_exact\n";
}

}
