from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "src" / "BenchmarkRunner.cpp").read_text(encoding="utf-8")


def test_overview_camera_helper_is_available_to_full_and_thesis_suites() -> None:
    helper_pos = CPP.index("void apply_overview_camera(BenchmarkCase& c, SurfaceScene scene)")
    thesis_pos = CPP.index("void BenchmarkRunner::build_thesis_figure_cases()")
    full_pos = CPP.index("void BenchmarkRunner::build_full_cases()")
    assert helper_pos < thesis_pos
    assert helper_pos < full_pos
    assert "const auto apply_overview_camera" not in CPP


def test_full_shading_normal_stage_uses_shared_overview_camera() -> None:
    full = CPP.split("void BenchmarkRunner::build_full_cases()", 1)[1]
    assert "Stage C2: shading-normal sensitivity" in full
    assert "apply_overview_camera(c, scene);" in full
