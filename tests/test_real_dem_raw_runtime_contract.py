from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
HEIGHTFIELD = (ROOT / "src" / "Heightfield.cpp").read_text(encoding="utf-8")
APP = (ROOT / "src" / "TerrainResearchApp.cpp").read_text(encoding="utf-8")


def test_runtime_has_no_libtiff_dependency() -> None:
    assert "GeoTiffHeightfield.cpp" not in CMAKE
    assert "gfx::research_base tiff" not in CMAKE
    assert "libtiff" not in CMAKE


def test_dem_runtime_uses_only_validated_raw() -> None:
    assert "mount_st_helens_usgs_10m_1921_f32.raw" in HEIGHTFIELD
    assert "expected_bytes" in HEIGHTFIELD
    assert "file_size" in HEIGHTFIELD
    assert "grid_.resize(sample_count)" in HEIGHTFIELD
    assert "grid_.data()" in HEIGHTFIELD
    assert "std::isfinite" in HEIGHTFIELD
    assert "load_mount_st_helens_geotiff" not in HEIGHTFIELD


def test_dem_contract_is_checked_before_upload_and_draw() -> None:
    assert "DEM CPU grid contract failed before OpenGL upload" in APP
    assert "DEM render contract failed before draw submission" in APP
    assert "expected_samples = 1921ull * 1921ull" in APP
