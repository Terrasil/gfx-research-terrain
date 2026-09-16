from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "src" / "TerrainResearchApp.cpp").read_text(encoding="utf-8")
GPU = (ROOT / "src" / "GpuTessellatedTerrain.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "include" / "gfx" / "terrain" / "GpuTessellatedTerrain.hpp").read_text(encoding="utf-8")
RUNNER = (ROOT / "src" / "BenchmarkRunner.cpp").read_text(encoding="utf-8")


def test_dem_uses_dense_heightmap_grid_and_summit_centered_camera() -> None:
    assert "return benchmark_.active() ? 120 : 48;" in APP
    assert "target_z = 0.0f" in APP
    assert "c.camera_target_z = 0.0f" in RUNNER
    assert "yaw = 2.34f" in APP


def test_dem_interactive_draw_does_not_issue_async_primitive_queries() -> None:
    assert "const bool measure_primitives = !dem_scene || synchronize_triangle_count;" in APP
    assert "bool measure_primitives = true" in HEADER
    assert "if (!measure_primitives)" in GPU


def test_query_object_is_not_reused_while_pending() -> None:
    assert "if (query_pending_[current_query])" in GPU
    assert "return;" in GPU


def test_real_dem_smoke_is_scientific_and_lightweight() -> None:
    block = RUNNER.split("// First case is deliberately a visible smoke test.", 1)[1].split("// External-validity core", 1)[0]
    assert "RenderMode::Scientific" in block
    assert "c.width = 1280" in block
    assert "c.height = 720" in block


def test_scene_rebuild_drains_pending_primitive_queries() -> None:
    assert "tessellated_terrain_.reset_measurement();" in APP
    assert "glGetQueryObjectuiv(primitive_queries_[i], GL_QUERY_RESULT, &ignored);" in GPU
    assert "query_index_ = 0;" in GPU
