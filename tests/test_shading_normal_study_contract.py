from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER = (ROOT / "src" / "BenchmarkRunner.cpp").read_text(encoding="utf-8")
APP = (ROOT / "src" / "TerrainResearchApp.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "include" / "gfx" / "terrain" / "ExperimentConfig.hpp").read_text(encoding="utf-8")
SHADER = (ROOT / "shaders" / "surface.frag").read_text(encoding="utf-8")
FIGURES = (ROOT / "tools" / "make_thesis_figures.py").read_text(encoding="utf-8")


def test_cli_and_config_expose_shading_normal_mode() -> None:
    assert "enum class ShadingNormalMode" in CONFIG
    assert "shading_normal_mode" in CONFIG
    assert "--smooth-normals" in MAIN
    assert "--geometric-normals" in MAIN
    assert "uShadingNormalMode" in SHADER


def test_benchmark_suite_contains_shading_normal_study_for_scientific_and_realistic_views() -> None:
    assert "BenchmarkStudy::ShadingNormalStudy" in RUNNER
    assert "RenderMode::Scientific" in RUNNER
    assert "RenderMode::Realistic" in RUNNER
    assert "ShadingNormalMode::Geometric" in RUNNER
    assert "ShadingNormalMode::Smooth" in RUNNER
    for scene_name in ["SmoothHills", "SharpRidge", "MixedFrequency", "CliffBand", "ErodedMountain", "PlateauCanyon", "AlpineEscarpment", "GlacialValley", "RiverBasin", "Badlands", "FracturedHighlands"]:
        assert f"SurfaceScene::{scene_name}" in RUNNER


def test_capture_filenames_and_csv_encode_render_and_normal_modes() -> None:
    assert '"-rm" + std::string(render_mode_name(test_case.benchmark_render_mode))' in APP
    assert '"-nm" + std::string(shading_normal_mode_name(test_case.shading_normal_mode))' in APP
    assert 'render_mode,shading_normal_mode' in RUNNER


def test_figure_generator_has_shading_normal_gallery_and_cliffband_gallery_assets() -> None:
    assert "shading_normal_gallery" in FIGURES
    assert "cliffband_camera_candidates" in FIGURES
    assert "Cliff Band" in FIGURES


def test_realistic_smooth_normal_cases_capture_diagnostics_for_all_target_scenes() -> None:
    assert "c.capture_diagnostics = mode == RenderMode::Realistic && normal_mode == ShadingNormalMode::Smooth;" in RUNNER


def test_figure_generator_exports_scene_specific_smooth_normal_galleries() -> None:
    assert "shading_normal_gallery_glacial" in FIGURES
    assert "shading_normal_gallery_cliffband" in FIGURES
    assert "shading_normal_gallery_alpine" in FIGURES


def test_full_benchmark_includes_formal_shading_normal_study() -> None:
    assert "Stage C2: shading-normal sensitivity" in RUNNER
    assert "c.study = BenchmarkStudy::ShadingNormalStudy;" in RUNNER
    assert "c.capture_images = true;" in RUNNER


def test_shading_normal_study_targets_all_original_procedural_scenes_in_both_suites() -> None:
    runner = RUNNER
    expected = "{SurfaceScene::SmoothHills, SurfaceScene::SharpRidge, SurfaceScene::MixedFrequency, SurfaceScene::CliffBand, SurfaceScene::ErodedMountain, SurfaceScene::PlateauCanyon, SurfaceScene::AlpineEscarpment, SurfaceScene::GlacialValley, SurfaceScene::RiverBasin, SurfaceScene::Badlands, SurfaceScene::FracturedHighlands}"
    assert runner.count(expected) >= 2
    # The measured DEM is evaluated in a separate external-validation suite so the frozen shading study remains comparable.
    assert "SurfaceScene::MountStHelensDem" in runner

