from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER = (ROOT / "src" / "BenchmarkRunner.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "include" / "gfx" / "terrain" / "BenchmarkRunner.hpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "include" / "gfx" / "terrain" / "ExperimentConfig.hpp").read_text(encoding="utf-8")
FIGURES = (ROOT / "tools" / "make_thesis_figures.py").read_text(encoding="utf-8")


def test_pbr_figure_study_is_declared_and_routable() -> None:
    assert "PbrFigure" in HEADER
    assert "thesis_pbr_figure_suite" in CONFIG
    assert 'case BenchmarkStudy::PbrFigure: return "pbr-figure";' in RUNNER
    assert "build_pbr_figure_cases();" in HEADER
    assert "build_pbr_figure_cases()" in RUNNER


def test_pbr_figure_cases_use_full_realistic_pbr_and_smooth_normals() -> None:
    assert "c.study = BenchmarkStudy::PbrFigure;" in RUNNER
    assert "c.method = ResearchMethod::TessellationContextAware;" in RUNNER
    assert "c.benchmark_render_mode = RenderMode::Realistic;" in RUNNER
    assert "c.shading_normal_mode = ShadingNormalMode::Smooth;" in RUNNER
    assert "c.capture_images = true;" in RUNNER
    assert "c.capture_diagnostics = true;" in RUNNER


def test_pbr_suite_covers_all_surface_scenes_and_camera_sweeps() -> None:
    for scene_name in [
        "SmoothHills", "SharpRidge", "MixedFrequency", "CliffBand", "ErodedMountain",
        "PlateauCanyon", "AlpineEscarpment", "GlacialValley", "RiverBasin", "Badlands",
        "FracturedHighlands",
    ]:
        assert f"SurfaceScene::{scene_name}" in RUNNER
    for yaw in ["1.92f", "2.18f", "2.44f", "2.06f", "2.32f", "2.58f", "2.84f"]:
        assert yaw in RUNNER


def test_figure_generator_knows_full_pbr_galleries() -> None:
    assert "scene_gallery_full_pbr" in FIGURES
    assert "cliff_band_pbr_candidates" in FIGURES
    assert "alpine_escarpment_pbr_candidates" in FIGURES


def test_gui_can_run_pbr_suite_without_validation_gate() -> None:
    app = (ROOT / "src" / "TerrainResearchApp.cpp").read_text(encoding="utf-8")
    header = (ROOT / "include" / "gfx" / "terrain" / "TerrainResearchApp.hpp").read_text(encoding="utf-8")
    assert "PbrFigures" in header
    assert 'Run PBR screenshot tests' in app
    assert 'suite_config.thesis_pbr_figure_suite = suite == GuiTestSuite::PbrFigures;' in app
    assert 'ImGui::BeginDisabled(!gui_validation_passed_)' not in app


def test_benchmark_does_not_force_scientific_for_realistic_cases() -> None:
    app = (ROOT / "src" / "TerrainResearchApp.cpp").read_text(encoding="utf-8")
    assert 'capture_render_mode_override_enabled_ ? capture_render_mode_ : experiment_config_.render_mode' in app
