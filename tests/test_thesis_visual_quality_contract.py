from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_alpine_and_cliffband_overviews_face_the_main_step_and_have_camera_sweeps():
    runner = (ROOT / "src/BenchmarkRunner.cpp").read_text(encoding="utf-8")
    header = (ROOT / "include/gfx/terrain/BenchmarkRunner.hpp").read_text(encoding="utf-8")
    assert "BenchmarkStudy::ThesisCamera" in runner
    assert "ThesisCamera" in header
    assert "SurfaceScene::CliffBand" in runner
    assert "c.camera_yaw = 2.32f" in runner
    for yaw in ("2.06f", "2.32f", "2.58f", "2.84f"):
        assert yaw in runner
    for yaw in ("1.92f", "2.18f", "2.44f"):
        assert yaw in runner
    assert "c.camera_distance = 15.0f" in runner
    assert "c.camera_pitch = 0.33f" in runner


def test_capture_filenames_encode_camera_orientation_and_fov():
    app = (ROOT / "src/TerrainResearchApp.cpp").read_text(encoding="utf-8")
    assert '"-yaw"' in app
    assert '"-pitch"' in app
    assert '"-fov"' in app
    assert "glm::degrees(test_case.camera_yaw)" in app
    assert "glm::degrees(test_case.camera_pitch)" in app


def test_thesis_primary_comparison_uses_scientific_shading():
    script = (ROOT / "tools/run_thesis_figures.ps1").read_text(encoding="utf-8")
    assert "--thesis-figures --scientific" in script


def test_tessellation_heatmap_legend_matches_gpu_quantity():
    figure_script = (ROOT / "tools/make_thesis_figures.py").read_text(encoding="utf-8")
    shader = (ROOT / "shaders/tess_surface.tesc").read_text(encoding="utf-8")
    assert "tcLevel = log2(max(maximumFactor, 1.0))" in shader
    assert "LOD_COLORS[:5]" in figure_script
    assert "log_2" in figure_script
    assert "0..4" in figure_script


def test_public_figure_wording_avoids_known_misleading_terms():
    figure_script = (ROOT / "tools/make_thesis_figures.py").read_text(encoding="utf-8").lower()
    assert "height variance" not in figure_script
    assert "surrogate" not in figure_script
    assert "surogat" not in figure_script
    assert "ablation" not in figure_script
    assert "metoda a wygrywa" not in figure_script
    assert "rms błędu rekonstrukcji" in figure_script
    assert "ograniczenie radiometryczne" in figure_script


def test_controller_diagram_does_not_imply_online_calibration_feedback():
    figure_script = (ROOT / "tools/make_thesis_figures.py").read_text(encoding="utf-8")
    block = figure_script.split("def controller_flow", 1)[1].split("def representation_capability", 1)[0]
    assert "Walidacja eksperymentalna" in block
    assert "porównanie z obrazem referencyjnym" in block
    assert "kalibruje" not in block
    assert "surogat" not in block
