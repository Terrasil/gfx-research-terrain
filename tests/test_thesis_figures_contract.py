from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_thesis_figure_cli_and_suite_exist():
    main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
    runner = (ROOT / "src/BenchmarkRunner.cpp").read_text(encoding="utf-8")
    header = (ROOT / "include/gfx/terrain/ExperimentConfig.hpp").read_text(encoding="utf-8")
    assert "--thesis-figures" in main
    assert "thesis_figure_suite" in header
    assert "build_thesis_figure_cases" in runner


def test_diagnostic_capture_modes_exist():
    app = (ROOT / "src/TerrainResearchApp.cpp").read_text(encoding="utf-8")
    for suffix in ("-realistic.png", "-lod.png", "-error.png", "-normal.png", "-margin.png", "-slope.png", "-rgb-error.png"):
        assert suffix in app
    assert "capture_render_mode_override_enabled_" in app


def test_figure_generation_scripts_exist():
    assert (ROOT / "tools/make_thesis_figures.py").is_file()
    assert (ROOT / "tools/run_thesis_figures.ps1").is_file()


def test_thesis_capture_uses_dedicated_overview_study_and_large_raw_frames():
    runner = (ROOT / "src/BenchmarkRunner.cpp").read_text(encoding="utf-8")
    header = (ROOT / "include/gfx/terrain/BenchmarkRunner.hpp").read_text(encoding="utf-8")
    assert "BenchmarkStudy::ThesisFigure" in runner
    assert "ThesisFigure" in header
    assert "capture_width = 2560" in runner
    assert "capture_height = 1440" in runner
    assert "SurfaceScene::AlpineEscarpment" in runner
    assert "SurfaceScene::CliffBand" in runner
    assert "c.camera_distance = 15.0f" in runner
    assert "c.camera_yaw = 2.18f" in runner
    assert "c.camera_yaw = 2.32f" in runner


def test_rgb_error_is_saved_from_test_render_targets():
    app = (ROOT / "src/TerrainResearchApp.cpp").read_text(encoding="utf-8")
    shader = (ROOT / "shaders/difference.frag").read_text(encoding="utf-8")
    assert "save_rgb_error_capture" in app
    assert "reference_fbo_.color()" in app
    assert "method_fbo_.color()" in app
    assert "uErrorMax" in shader


def test_diagnostic_heatmaps_are_not_lighting_modulated():
    shader = (ROOT / "shaders/surface.frag").read_text(encoding="utf-8")
    block = shader.split("if (uRenderMode == 2)", 1)[1].split("if (uRenderMode == 4)", 1)[0]
    assert "c * light" not in block
    assert "lodColor(fs.lodLevel)" in block
    assert "errorColor(fs.errorRatio)" in block


def test_thesis_capture_revision_is_written_to_manifest():
    runner = (ROOT / "src/BenchmarkRunner.cpp").read_text(encoding="utf-8")
    assert "thesis_capture_revision=v6.11" in runner


def test_pbr_thesis_suite_and_script_exist() -> None:
    main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
    assert "--thesis-figures-pbr" in main
    script = (ROOT / "tools" / "run_thesis_figures_pbr.ps1").read_text(encoding="utf-8")
    assert "--thesis-figures-pbr --realistic --smooth-normals" in script
    assert "make_thesis_figures.py" in script
