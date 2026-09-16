from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source() -> str:
    return (Path(__file__).resolve().parents[1] / "src/TerrainResearchApp.cpp").read_text(encoding="utf-8")


def test_gui_exposes_validation_button() -> None:
    text = source()
    assert 'ImGui::Button("Run validation tests")' in text
    assert 'start_gui_test_suite(GuiTestSuite::Validation, frame)' in text


def test_doctoral_quick_is_available_without_validation_gate() -> None:
    text = source()
    assert 'ImGui::Button("Run doctoral quick")' in text
    assert 'start_gui_test_suite(GuiTestSuite::DoctoralQuick, frame)' in text
    assert 'ImGui::BeginDisabled(!gui_validation_passed_)' not in text


def test_doctoral_full_is_available_without_validation_gate() -> None:
    text = source()
    assert 'ImGui::Button("Run doctoral full")' in text
    assert 'start_gui_test_suite(GuiTestSuite::DoctoralFull, frame)' in text
    assert 'ImGui::BeginDisabled(!gui_validation_passed_)' not in text


def test_gui_exposes_independent_pbr_screenshot_test_button() -> None:
    text = source()
    assert 'ImGui::Button("Run PBR screenshot tests")' in text
    assert 'start_gui_test_suite(GuiTestSuite::PbrFigures, frame)' in text
    assert 'suite_config.thesis_pbr_figure_suite = suite == GuiTestSuite::PbrFigures;' in text
    assert 'output_name = "thesis-pbr-gui"' in text


def test_gui_python_launcher_does_not_use_shell_on_windows() -> None:
    text = source()
    assert "_wspawnvp(_P_WAIT" in text
    assert "std::system(" not in text
    assert "cmd.exe" not in text


def test_gui_exposes_shading_normal_mode_selector() -> None:
    text = source()
    assert 'ImGui::Combo("Shading normals"' in text
    assert "Geometric (faceted)" in text
    assert "Smooth interpolated" in text


def test_benchmark_uses_case_render_mode_instead_of_forcing_scientific() -> None:
    text = source()
    assert 'capture_render_mode_override_enabled_ ? capture_render_mode_ : experiment_config_.render_mode' in text
    assert 'capture_render_mode_override_enabled_ ? capture_render_mode_ : RenderMode::Scientific' not in text


def test_fatal_cpp_exceptions_are_written_to_results_log() -> None:
    main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
    assert "last_crash.txt" in main
    assert "catch (const std::exception& error)" in main
