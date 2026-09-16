from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "src" / "TerrainResearchApp.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
TESC = (ROOT / "shaders" / "tess_surface.tesc").read_text(encoding="utf-8")


def test_real_dem_scene_selection_defers_legacy_cpu_structures() -> None:
    assert "if (scene_ != SurfaceScene::MountStHelensDem)" in APP
    assert "ensure_cpu_scene_structures();" in APP
    assert "cpu_scene_structures_ready_" in APP


def test_real_dem_uses_stable_full_extent_working_grid() -> None:
    assert "return benchmark_.active() ? 120 : 48;" in APP
    assert "const int patch_grid = active_tessellation_patch_grid();" in APP
    assert "tessellated_terrain_.draw(patch_grid" in APP


def test_dense_dem_reference_uses_tessellation_path() -> None:
    assert "dem_dense_reference" in APP
    assert "uForceMaxTessellation" in APP
    assert "uniform bool uForceMaxTessellation" in TESC
    assert "if (uForceMaxTessellation)" in TESC


def test_runtime_crash_diagnostics_are_written() -> None:
    assert "last_scene_rebuild.txt" in APP
    assert "last_dem_runtime.txt" in APP
    assert "glFinish();" in APP
    assert "SetUnhandledExceptionFilter" in MAIN
    assert "Windows structured exception" in MAIN
