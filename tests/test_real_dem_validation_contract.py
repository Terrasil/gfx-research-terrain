from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG = (ROOT / "include" / "gfx" / "terrain" / "ExperimentConfig.hpp").read_text(encoding="utf-8")
HEIGHTFIELD_HPP = (ROOT / "include" / "gfx" / "terrain" / "Heightfield.hpp").read_text(encoding="utf-8")
HEIGHTFIELD_CPP = (ROOT / "src" / "Heightfield.cpp").read_text(encoding="utf-8")
RUNNER_HPP = (ROOT / "include" / "gfx" / "terrain" / "BenchmarkRunner.hpp").read_text(encoding="utf-8")
RUNNER_CPP = (ROOT / "src" / "BenchmarkRunner.cpp").read_text(encoding="utf-8")
APP_HPP = (ROOT / "include" / "gfx" / "terrain" / "TerrainResearchApp.hpp").read_text(encoding="utf-8")
APP_CPP = (ROOT / "src" / "TerrainResearchApp.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
SETUP = (ROOT / "tools" / "setup_mount_st_helens_dem.py").read_text(encoding="utf-8")

ASSET = ROOT / "assets" / "dem" / "mount_st_helens_usgs_10m_1921_f32.raw"
OLD_ASSET = ROOT / "assets" / "dem" / "mount_st_helens_full_extent_20m_f32.raw"
OLD_MASK = ROOT / "assets" / "dem" / "mount_st_helens_full_extent_valid_u8.raw"


def test_real_dem_loader_targets_dense_regular_heightmap() -> None:
    assert "MountStHelensDem" in CONFIG
    assert "surface_scene_count = 12" in CONFIG
    assert "load_mount_st_helens_dem" in HEIGHTFIELD_HPP
    assert "constexpr int source_size = 1921;" in HEIGHTFIELD_CPP
    assert "constexpr float source_center_span_m = 19200.0f;" in HEIGHTFIELD_CPP
    assert 'mount_st_helens_usgs_10m_1921_f32.raw' in HEIGHTFIELD_CPP
    assert "validity_mask_.clear();" in HEIGHTFIELD_CPP
    assert "std::minmax_element" in HEIGHTFIELD_CPP
    assert "world_size_ / source_center_span_m" in HEIGHTFIELD_CPP
    assert ASSET.exists()
    assert ASSET.stat().st_size == 1921 * 1921 * 4


def test_old_irregular_lidar_mask_is_not_an_active_asset() -> None:
    assert not OLD_ASSET.exists()
    assert not OLD_MASK.exists()
    assert "full_extent_valid" not in HEIGHTFIELD_CPP


def test_dense_dem_setup_uses_public_seamless_sources_and_rejects_holes() -> None:
    assert "prd-tnm.s3.amazonaws.com" in SETUP
    assert "USGS_13_n47w123_20250813.tif" in SETUP
    assert "USGS_13_n47w123" in SETUP
    assert "TARGET_SIZE = 1921" in SETUP
    assert "TARGET_SPACING_M = 10.0" in SETUP
    assert 'TARGET_CRS = "EPSG:26910"' in SETUP
    assert "if not np.isfinite(destination).all()" in SETUP
    assert "no hole filling" in SETUP.lower() or "does not fill" in SETUP.lower()
    assert (ROOT / "tools" / "setup_mount_st_helens_dem.ps1").exists()


def test_real_dem_patch_grid_matches_1921_samples_exactly() -> None:
    assert "return benchmark_.active() ? 120 : 48;" in APP_CPP
    assert "120 patches x tessellation 16" in APP_CPP


def test_missing_dense_asset_is_reported_without_forcing_application_exit() -> None:
    assert "scene_load_error_" in APP_HPP
    assert "const SurfaceScene previous_scene = scene_;" in APP_CPP
    assert "catch (const std::exception& e)" in APP_CPP
    assert "scene_ = previous_scene;" in APP_CPP
    assert "Mount St. Helens DEM RAW asset is missing" in HEIGHTFIELD_CPP
    assert "file_size" in HEIGHTFIELD_CPP


def test_gui_uses_validated_raw_without_runtime_downloader_and_can_run_validation() -> None:
    assert 'ImGui::Button("Build canonical DEM RAW (optional)")' not in APP_CPP
    assert '"tools" / "setup_mount_st_helens_dem.py"' not in APP_CPP
    assert 'ImGui::Button("Run real DEM tests")' in APP_CPP
    assert "start_gui_test_suite(GuiTestSuite::RealDemValidation, frame)" in APP_CPP
    assert "validated canonical 10 m RAW" in APP_CPP
    assert "raw_ready" in APP_CPP
    assert "if (!raw_ready)" in APP_CPP
    assert "direct_ready" not in APP_CPP
    assert '--real-dem-validation' in MAIN
    assert (ROOT / "tools" / "run_real_dem_validation.ps1").exists()
    assert (ROOT / "tools" / "analyze_real_dem.py").exists()


def test_real_dem_suite_is_separate_from_doctoral_full() -> None:
    assert "RealDemValidation" in RUNNER_HPP
    assert "build_real_dem_validation_cases" in RUNNER_HPP
    assert "real_dem_validation_suite" in CONFIG
    assert "else if (config_.real_dem_validation_suite) build_real_dem_validation_cases();" in RUNNER_CPP
    assert "void BenchmarkRunner::build_real_dem_validation_cases()" in RUNNER_CPP


def test_real_dem_suite_keeps_expected_67_case_design() -> None:
    section = RUNNER_CPP.split("void BenchmarkRunner::build_real_dem_validation_cases()", 1)[1]
    section = section.split("void BenchmarkRunner::write_manifest", 1)[0]
    assert "std::array<float, 3> distances" in section
    assert "std::array<float, 6> budgets" in section
    assert "TessellationVarianceBaseline" in section
    assert "TessellationError" in section
    assert "for (const int bias : {-2, -1, 0, 1, 2})" in section
    assert "TessellationNormalBound" in section
    assert "TessellationContextAware" in section
    assert "c.mode = BenchmarkMode::Null;" in section
    assert "c.benchmark_render_mode = RenderMode::Realistic;" in section
    assert "c.capture_diagnostics = method == ResearchMethod::TessellationError;" in section


def test_manifest_records_dense_real_dem_provenance() -> None:
    assert "real_dem_dataset=USGS 3DEP 1/3 arc-second DEM" in RUNNER_CPP
    assert "real_dem_source_tile=USGS_13_n47w123_20250813.tif" in RUNNER_CPP
    assert "real_dem_working_grid=1921x1921 samples in EPSG:26910 at 10 m spacing" in RUNNER_CPP
    assert "real_dem_crop_span_m=19200" in RUNNER_CPP
    assert "real_dem_nodata_handling=none inside the prepared crop" in RUNNER_CPP
    assert "no hole filling or synthetic extrapolation" in RUNNER_CPP


def test_real_dem_suite_starts_with_capture_smoke_case() -> None:
    real = RUNNER_CPP.split("void BenchmarkRunner::build_real_dem_validation_cases()", 1)[1]
    core = real.index("// External-validity core")
    assert real.index("visible smoke test") < core
    assert "c.capture_images = true;" in real[:core]


def test_procedural_grid_size_is_restored_after_real_dem() -> None:
    assert "if (scene_ != SurfaceScene::MountStHelensDem) grid_size_ = 513;" in HEIGHTFIELD_CPP
