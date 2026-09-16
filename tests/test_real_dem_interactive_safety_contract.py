from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "src" / "TerrainResearchApp.cpp").read_text(encoding="utf-8")


def test_interactive_dem_uses_bounded_patch_grid_but_benchmark_keeps_dense_grid() -> None:
    assert "return benchmark_.active() ? 120 : 48;" in APP
    assert "120 patches x tessellation 16" in APP


def test_continuous_dem_does_not_allocate_full_size_validity_texture() -> None:
    assert 'write_dem_runtime_stage("creating compact validity fallback")' in APP
    assert "glTextureStorage2D(heightfield_validity_texture_, 1, GL_R8, 1, 1)" in APP


def test_dem_stages_are_visible_in_console() -> None:
    assert 'std::cerr << "[DEM] " << stage' in APP
    assert 'write_dem_runtime_stage("uploading height texture samples")' in APP
    assert 'write_dem_runtime_stage("tessellation draw submitted", patch_grid)' in APP
