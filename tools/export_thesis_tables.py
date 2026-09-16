from __future__ import annotations

import argparse
import csv
from pathlib import Path


def rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def esc(text: object) -> str:
    return str(text).replace("_", r"\_").replace("%", r"\%")


def fnum(value: str, digits: int = 4) -> str:
    try:
        return f"{float(value):.{digits}f}"
    except (TypeError, ValueError):
        return "--"


def write_table(path: Path, caption: str, label: str, headers: list[str], data: list[list[str]]) -> None:
    cols = "l" + "r" * (len(headers) - 1)
    with path.open("w", encoding="utf-8") as f:
        f.write("\\begin{table}[htbp]\n\\centering\n\\small\n")
        f.write(f"\\caption{{{caption}}}\n\\label{{{label}}}\n")
        f.write("\\resizebox{\\textwidth}{!}{%\n")
        f.write(f"\\begin{{tabular}}{{{cols}}}\n\\toprule\n")
        f.write(" & ".join(headers) + r" \\" + "\n\\midrule\n")
        for row in data:
            f.write(" & ".join(row) + r" \\" + "\n")
        f.write("\\bottomrule\n\\end{tabular}%\n}\n\\end{table}\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("results", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    cases = rows(args.results / "cases.csv")
    matched = rows(args.results / "matched_quality.csv")
    context = rows(args.results / "context_gain.csv")
    silhouette = rows(args.results / "silhouette.csv")

    baseline_rows = []
    for r in cases:
        if r.get("study") != "strong-baseline":
            continue
        baseline_rows.append([
            esc(r.get("scene", "")), esc(r.get("method", "")),
            fnum(r.get("gpu_median_ms", ""), 4), fnum(r.get("triangles_median", ""), 0),
            fnum(r.get("image_foreground_rmse", ""), 5), fnum(r.get("boundary_p95_distance_px", ""), 2),
        ])
    write_table(
        args.output / "terrain_baseline.tex",
        "Wyniki pilotażowe terenu: koszt i zmierzona jakość dla przypadków strong-baseline.",
        "tab:terrain-pilot-baseline",
        ["Scena", "Metoda", "GPU [ms]", "Trójkąty", "FG RMSE", "Granica p95 [px]"],
        baseline_rows,
    )

    matched_rows = [[
        esc(r.get("scene", "")), esc(r.get("baseline", "")), esc(r.get("candidate", "")),
        fnum(str(100.0 * float(r.get("quality_gap_fraction", 0.0))), 1),
        fnum(str(100.0 * float(r.get("gpu_change_fraction", 0.0))), 1),
        fnum(str(100.0 * float(r.get("triangle_change_fraction", 0.0))), 1),
    ] for r in matched]
    write_table(
        args.output / "terrain_matched_quality.tex",
        "Porównania pilotażowe matched-quality. Ujemna zmiana kosztu oznacza przewagę kandydata.",
        "tab:terrain-pilot-matched",
        ["Scena", "Baseline", "Kandydat", "Różnica jakości [\\%]", "$\\Delta$GPU [\\%]", "$\\Delta$tri [\\%]"],
        matched_rows,
    )

    context_rows = [[
        esc(r.get("scene", "")), fnum(r.get("scientific_roughness", ""), 2),
        fnum(r.get("light_azimuth_degrees", ""), 0), fnum(r.get("radiance_budget", ""), 3),
        fnum(str(100.0 * float(r.get("rmse_change_fraction", 0.0))), 1),
        fnum(str(100.0 * float(r.get("gpu_change_fraction", 0.0))), 1),
    ] for r in context]
    write_table(
        args.output / "terrain_context.tex",
        "Zmiana wariantu z ograniczeniem radiometrycznym względem wariantu z progiem odchylenia normalnej w badaniu pilotażowym.",
        "tab:terrain-pilot-context",
        ["Scena", "Roughness", "Azymut światła", "$\\epsilon_L$", "$\\Delta$RMSE [\\%]", "$\\Delta$GPU [\\%]"],
        context_rows,
    )

    sil_rows = [[
        esc(r.get("scene", "")), esc(r.get("method", "")), fnum(r.get("camera_pitch", ""), 2),
        fnum(r.get("coverage_mismatch_fraction", ""), 6), fnum(r.get("boundary_p95_distance_px", ""), 2),
        fnum(r.get("image_foreground_rmse", ""), 5),
    ] for r in silhouette]
    write_table(
        args.output / "terrain_silhouette.tex",
        "Pilotażowe wyniki silhouette stress.",
        "tab:terrain-pilot-silhouette",
        ["Scena", "Metoda", "Pitch", "Coverage mismatch", "Granica p95 [px]", "FG RMSE"],
        sil_rows,
    )

    print(f"wrote LaTeX tables to {args.output}")


if __name__ == "__main__":
    main()
