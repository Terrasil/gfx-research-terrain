from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

try:
    import numpy as np
    import rasterio
    from rasterio.enums import Resampling
    from rasterio.transform import Affine
    from rasterio.warp import reproject, transform
except ImportError:
    if os.environ.get("GFX_DEM_DEP_BOOTSTRAPPED") == "1":
        raise SystemExit("Could not import numpy/rasterio after dependency installation.")
    print("numpy/rasterio not found; installing the DEM preparation dependencies for the current user...")
    result = subprocess.run([sys.executable, "-m", "pip", "install", "--user", "numpy", "rasterio"], check=False)
    if result.returncode != 0:
        raise SystemExit("Could not install numpy/rasterio. Run: python -m pip install --user numpy rasterio")
    env = os.environ.copy()
    env["GFX_DEM_DEP_BOOTSTRAPPED"] = "1"
    os.execve(sys.executable, [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]], env)


CENTER_LAT = 46.1914
CENTER_LON = -122.1956
# The active USGS 1/3 arc-second tile is NAD83 (EPSG:4269); keep the metric
# working grid in the corresponding NAD83 / UTM zone 10N frame.
TARGET_CRS = "EPSG:26910"
TARGET_SIZE = 1921
TARGET_SPACING_M = 10.0
TARGET_SPAN_M = (TARGET_SIZE - 1) * TARGET_SPACING_M
OUTPUT_NAME = "mount_st_helens_usgs_10m_1921_f32.raw"
META_NAME = "mount_st_helens_usgs_10m_1921_metadata.json"

USGS_TILE = "n47w123"
USGS_SOURCE_FILENAME = "USGS_13_n47w123_20250813.tif"
USGS_CATALOG_URL = "https://data.usgs.gov/datacatalog/data/USGS:3a81321b-c153-416f-98b7-cc8e5f0e17c3"
# The first URL reproduces the exact source used for the v6.12 archive.  The
# undated current-tile form is retained as a compatibility fallback because
# The National Map can update names in the current directory over time.
USGS_URLS = [
    (
        "https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/current/"
        f"{USGS_TILE}/{USGS_SOURCE_FILENAME}"
    ),
    (
        "https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/current/"
        f"{USGS_TILE}/USGS_13_{USGS_TILE}.tif"
    ),
]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def download(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": "gfx-research-terrain/6.12"})
    print(f"Downloading {url}")
    with urllib.request.urlopen(request, timeout=120) as response, destination.open("wb") as out:
        total_header = response.headers.get("Content-Length")
        total = int(total_header) if total_header and total_header.isdigit() else 0
        done = 0
        while True:
            block = response.read(1024 * 1024)
            if not block:
                break
            out.write(block)
            done += len(block)
            if total:
                print(f"  {done / 1024 / 1024:.1f} / {total / 1024 / 1024:.1f} MiB", end="\r")
        if total:
            print()


def supplied_source_label(path: Path) -> tuple[str, str]:
    name = path.name.lower()
    if name.startswith("usgs_13_n47w123"):
        return "USGS 3DEP 1/3 arc-second DEM", USGS_CATALOG_URL
    return "user-supplied seamless GeoTIFF", "local user-supplied source; see source_file and source_sha256"


def obtain_source(cache_dir: Path, supplied: Path | None) -> tuple[Path, str, str]:
    if supplied is not None:
        supplied = supplied.resolve()
        if not supplied.exists():
            raise SystemExit(f"Supplied DEM does not exist: {supplied}")
        label, provenance = supplied_source_label(supplied)
        return supplied, label, provenance

    cache_dir.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []
    for index, url in enumerate(USGS_URLS):
        filename = USGS_SOURCE_FILENAME if index == 0 else f"USGS_13_{USGS_TILE}.tif"
        path = cache_dir / filename
        if path.exists() and path.stat().st_size > 1024 * 1024:
            print(f"Using cached source: {path}")
            return path, "USGS 3DEP 1/3 arc-second DEM", url
        try:
            download(url, path)
            if path.stat().st_size <= 1024 * 1024:
                raise RuntimeError("downloaded file is unexpectedly small")
            return path, "USGS 3DEP 1/3 arc-second DEM", url
        except (OSError, urllib.error.URLError, RuntimeError) as exc:
            errors.append(f"{url}: {exc}")
            path.unlink(missing_ok=True)

    raise SystemExit(
        "Unable to download the USGS 3DEP 1/3 arc-second DEM automatically.\n"
        + "\n".join(errors)
        + "\nDownload tile n47w123 from The National Map and rerun with --source <USGS_13_n47w123*.tif>."
    )


def prepare(source_path: Path, output_dir: Path, source_label: str, source_url: str) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    out_raw = output_dir / OUTPUT_NAME
    out_meta = output_dir / META_NAME

    with rasterio.open(source_path) as src:
        if src.crs is None:
            raise SystemExit("Source GeoTIFF has no CRS.")
        if src.count < 1:
            raise SystemExit("Source GeoTIFF has no elevation band.")

        xs, ys = transform("EPSG:4326", TARGET_CRS, [CENTER_LON], [CENTER_LAT])
        center_x = float(xs[0])
        center_y = float(ys[0])

        # Pixel centers span exactly 19.2 km: 1920 intervals * 10 m.
        half_span = TARGET_SPAN_M * 0.5
        left_edge = center_x - half_span - TARGET_SPACING_M * 0.5
        top_edge = center_y + half_span + TARGET_SPACING_M * 0.5
        dst_transform = Affine(
            TARGET_SPACING_M, 0.0, left_edge,
            0.0, -TARGET_SPACING_M, top_edge,
        )
        destination = np.full((TARGET_SIZE, TARGET_SIZE), np.nan, dtype=np.float32)

        reproject(
            source=rasterio.band(src, 1),
            destination=destination,
            src_transform=src.transform,
            src_crs=src.crs,
            src_nodata=src.nodata,
            dst_transform=dst_transform,
            dst_crs=TARGET_CRS,
            dst_nodata=np.nan,
            resampling=Resampling.bilinear,
        )

        if not np.isfinite(destination).all():
            missing = int(np.size(destination) - np.isfinite(destination).sum())
            raise SystemExit(
                f"Prepared crop still contains {missing} missing samples. "
                "This source is not seamless over the requested Mount St. Helens extent."
            )

        destination.astype("<f4", copy=False).tofile(out_raw)

        metadata = {
            "dataset": source_label,
            "source_catalog": USGS_CATALOG_URL if source_label.startswith("USGS 3DEP") else None,
            "source_url": source_url,
            "source_file": source_path.name,
            "source_sha256": sha256(source_path),
            "source_size_bytes": source_path.stat().st_size,
            "source_crs": str(src.crs),
            "source_dimensions": [src.width, src.height],
            "source_resolution": list(src.res),
            "source_nodata": None if src.nodata is None else float(src.nodata),
            "crop_center_lat_lon": [CENTER_LAT, CENTER_LON],
            "working_crs": TARGET_CRS,
            "working_dimensions": [TARGET_SIZE, TARGET_SIZE],
            "working_sample_spacing_m": TARGET_SPACING_M,
            "working_center_to_center_span_m": TARGET_SPAN_M,
            "working_extent_description": "19.2 km square centered on Mount St. Helens summit",
            "resampling": "bilinear reprojection to regular NAD83 / UTM zone 10N grid; no hole filling or extrapolation",
            "valid_samples": int(np.isfinite(destination).sum()),
            "elevation_min_m": float(destination.min()),
            "elevation_max_m": float(destination.max()),
            "output_height_raw": out_raw.name,
            "output_height_raw_sha256": sha256(out_raw),
            "output_format": "little-endian IEEE-754 float32, row-major, north-to-south rows",
        }
        out_meta.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

    expected = TARGET_SIZE * TARGET_SIZE * 4
    if out_raw.stat().st_size != expected:
        raise SystemExit(f"Unexpected output size: {out_raw.stat().st_size}, expected {expected}")

    print(f"Prepared USGS 10 m DEM: {out_raw}")
    print(f"Metadata: {out_meta}")
    print(f"Grid: {TARGET_SIZE} x {TARGET_SIZE}, spacing {TARGET_SPACING_M:.0f} m, span {TARGET_SPAN_M/1000:.1f} km")
    print(f"Elevation range: {metadata['elevation_min_m']:.1f} .. {metadata['elevation_max_m']:.1f} m")


def main() -> None:
    parser = argparse.ArgumentParser(description="Prepare the USGS 3DEP 1/3 arc-second Mount St. Helens heightmap.")
    parser.add_argument("--source", type=Path, help="Optional local USGS 1/3 arc-second GeoTIFF instead of automatic download.")
    parser.add_argument("--output-dir", type=Path, default=None)
    parser.add_argument("--keep-source", action="store_true", help="Keep an automatically downloaded GeoTIFF in assets/dem/source_cache.")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    output_dir = args.output_dir or (root / "assets" / "dem")
    cache_dir = output_dir / "source_cache"
    source_path, source_label, source_url = obtain_source(cache_dir, args.source)
    downloaded_to_cache = args.source is None and source_path.parent == cache_dir

    prepare(source_path, output_dir, source_label, source_url)

    if downloaded_to_cache and not args.keep_source:
        source_path.unlink(missing_ok=True)
        try:
            cache_dir.rmdir()
        except OSError:
            pass
        print("Removed downloaded source GeoTIFF after preparing the compact research asset.")


if __name__ == "__main__":
    main()
