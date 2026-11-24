#!/usr/bin/env python3
"""Validate paired DEM measurements and report draw, geometry and image deltas."""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path

from precompute_dem_features import sha

ROOT = Path(__file__).resolve().parents[1]


def read_csv(path):
    with path.open(newline="",encoding="utf-8") as f:
        return list(csv.DictReader(f))


def finite_number(row,name):
    out = float(row[name])
    if not math.isfinite(out):
        raise ValueError(f"Nonfinite {name}")
    return out


def extract_pairs(raw_rows,quality_rows,manifest=None):
    by_case = {}
    for row in raw_rows:
        if row["study"] == "offline-feature-study":
            by_case.setdefault(row["case"],[]).append(row)
    quality_by_case = {r["case"]:r for r in quality_rows}
    if len(quality_rows) != len(quality_by_case) or by_case.keys() != quality_by_case.keys():
        raise ValueError("Raw and quality cases must match")
    if manifest and (manifest.get("completed") != "1" or len(by_case) != int(manifest["case_count"])):
        raise ValueError("Incomplete DEM feature run")
    arms = {}
    for index,frames in by_case.items():
        base = frames[0]
        method = base["method"]
        is_map = int(base["offline_feature_map"])
        marker = ("offline" if is_map else "runtime") if method.endswith("feature") else (
            "distance" if method == "tessellation-distance" else "projected")
        yaw = round(float(base["camera_yaw"]),6) if base.get("camera_yaw") else ""
        key = (base.get("mode","static"), yaw, float(base["base_distance"]), float(base["error_budget_px"]))
        if manifest:
            key = (key[0],yaw,round(key[2],6),key[3])
        group = arms.setdefault(key,{})
        if marker in group:
            raise ValueError(f"Duplicate {marker} at {key}")
        quality = quality_by_case[index]
        if int(quality["offline_feature_map"]) != is_map:
            raise ValueError(f"Feature flag differs across CSVs in case {index}")
        numbers = [finite_number(row,"gpu_ms") for row in frames]
        counts = [int(row["triangles"]) for row in frames]
        if min(numbers)<=0 or min(counts)<=0:
            raise ValueError(f"Zero elapsed time or triangles in case {index}")
        frame_ids = None
        if manifest:
            frame_ids = [int(f["frame"]) for f in frames]
            warmup, samples = int(manifest["warmup_frames"]), int(manifest["sample_frames"])
            if frame_ids != list(range(warmup,warmup+samples)):
                raise ValueError(f"Wrong timing samples in case {index}")
        churn = statistics.mean(abs(a-b) for a,b in zip(counts,counts[1:]))/statistics.mean(counts) if len(counts)>1 else None
        group[marker] = dict(gpu=statistics.median(numbers), triangles=statistics.median(counts),
                             img=finite_number(quality,"image_foreground_rmse"),
                             boundary=finite_number(quality,"boundary_p95_distance_px"),churn=churn,
                             camera_trace=[round(float(f["camera_distance"]),6) for f in frames] if manifest else [], all_times=numbers)
    required={"runtime","offline","projected","distance"} if manifest else {"runtime","offline","projected"}
    expected_groups=int(manifest["case_count"])//len(required) if manifest else 6
    if len(arms)!=expected_groups:
        raise ValueError(f"Expected {expected_groups} groups, got {len(arms)}")
    for key,methods in arms.items():
        if set(methods)!=required or len({tuple(arm["camera_trace"]) for arm in methods.values()})!=1:
            raise ValueError(f"Incomplete pair or mismatched motion trace at {key}")
    return arms


def run(directory):
    root=Path(directory)
    manifest_path=root/"manifest.txt"
    manifest=dict(line.split("=",1) for line in manifest_path.read_text().splitlines() if "=" in line) if manifest_path.is_file() else {}
    schema2=manifest.get("offline_feature_schema")=="2"
    pairs=extract_pairs(read_csv(root/"raw.csv"),read_csv(root/"quality.csv"),manifest if schema2 else None)
    if schema2:
        dem=ROOT/"assets/dem"
        feature=dem/"mount_st_helens_features_v1_f32.raw"
        record=json.loads(feature.with_suffix(".json").read_text())
        if sha(feature)!=record["feature_sha256"] or sha(dem/record["source_file"])!=record["source_sha256"]:
            raise ValueError("DEM or feature file differs from the frozen input")
        (root/"offline_feature_inputs.json").write_text(json.dumps(record,indent=2)+"\n",encoding="utf-8")

    rows=[]
    for (mode,yaw,distance,budget),arms in sorted(pairs.items()):
        reference = arms["runtime"]
        out=dict(mode=mode,yaw_radians=yaw,distance=distance,budget_px=budget)
        for method,conditions in arms.items():
            out.update({f"{method}_{k}":conditions[k] for k in ("gpu","triangles","img","boundary","churn")})
        out["offline_gpu_change_pct"]=100*(arms["offline"]["gpu"]/reference["gpu"]-1)
        out["offline_triangles_change_pct"]=100*(arms["offline"]["triangles"]/reference["triangles"]-1)
        out["offline_foreground_rmse_delta"]=arms["offline"]["img"]-reference["img"]
        out["offline_boundary_p95_delta"]=arms["offline"]["boundary"]-reference["boundary"]
        out["median_paired_delta_ms"]=statistics.median(a-b for a,b in zip(arms["offline"]["all_times"],reference["all_times"]))
        rows.append(out)
    schema=list(rows[0])
    with (root/"offline_feature_pairs.csv").open("w",newline="",encoding="utf-8") as f:
        # Every group has the same set of arms; normalize field order across rotated runs.
        writer=csv.DictWriter(f,fieldnames=schema)
        writer.writeheader()
        writer.writerows(rows)
    print(f"Validated {len(rows)} paired camera and budget settings.")


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("results",type=Path)
    run(p.parse_args().results)
