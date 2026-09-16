from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "src" / "BenchmarkRunner.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "include" / "gfx" / "terrain" / "BenchmarkRunner.hpp").read_text(encoding="utf-8")


def test_validation_includes_projection_and_constraint_laws() -> None:
    assert "BenchmarkStudy::ViewAngleScaling" in CPP
    assert "BenchmarkStudy::FovScaling" in CPP
    assert "normal-budget monotonicity" not in CPP  # validator owns the verdict, benchmark owns the cases
    assert "1000000.0f" in CPP


def test_quick_suite_contains_calibration_negative_and_temporal_cases() -> None:
    quick = CPP.split("void BenchmarkRunner::build_quick_cases()", 1)[1]
    quick = quick.split("void BenchmarkRunner::build_full_cases()", 1)[0]
    assert "BenchmarkStudy::EstimatorCalibration" in quick
    assert "BenchmarkStudy::NegativeControl" in quick
    assert "BenchmarkStudy::TemporalStability" in quick
    assert "BenchmarkStudy::ViewAngleScaling" in quick
    assert "BenchmarkStudy::FovScaling" in quick


def test_benchmark_case_records_fov() -> None:
    assert "camera_fov_degrees" in HEADER
    assert '"camera_fov_degrees' in CPP
