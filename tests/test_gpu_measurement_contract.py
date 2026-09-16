from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GPU_CPP = (ROOT / "src" / "GpuTessellatedTerrain.cpp").read_text(encoding="utf-8")
APP_CPP = (ROOT / "src" / "TerrainResearchApp.cpp").read_text(encoding="utf-8")


def test_first_benchmark_frame_synchronizes_primitive_count() -> None:
    assert "benchmark_.frame_in_case() == 0" in APP_CPP
    assert "synchronize_triangle_count" in APP_CPP
    assert "GL_QUERY_RESULT, &primitives" in GPU_CPP


def test_primitive_measurement_is_reset_between_benchmark_cases() -> None:
    assert "tessellated_terrain_.reset_measurement();" in APP_CPP
    assert "query_pending_.fill(false);" in GPU_CPP
    assert "last_triangle_count_ = 0;" in GPU_CPP
