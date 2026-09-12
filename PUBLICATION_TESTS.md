# Preregistered publication tests

These hypotheses define the first terrain experiment. They should be changed only before the definitive publication run, with the revision recorded.

## H1 — error-budget calibration

**Independent variable:** requested pixel error budget.

**Baseline/reference:** finest available terrain mesh for rendered quality; analytic heightfield for offline representation error.

**Metrics:** foreground linear-HDR RMSE, depth RMSE, maximum/mean predicted geometric error, selected triangles.

**Test:** decreasing the requested error budget should monotonically reduce or preserve rendered error in aggregate. Cases in which the estimator threshold is satisfied but measured error behaves non-monotonically must be reported, not filtered out.

## H2 — efficiency at matched quality

**Independent variable:** controller and its quality parameter.

**Baseline:** distance-only LOD with bias sweep `{-1,0,1,2}`.

**Proposed:** error-budget sweep `{0.25,0.5,1.0,2.0}` pixels.

**Metrics:** GPU milliseconds, triangles and foreground image RMSE.

**Success criterion:** the proposed curve contains at least one non-dominated region where it reaches equal-or-lower error with lower work/cost, or lower error at comparable work, without relying on a single hand-picked operating point.

A result in which the distance baseline dominates is a valid negative result.

## H3 — feature preservation

**Independent variable:** feature term disabled/enabled.

**Scenes:** sharp ridge and cliff band are primary; smooth hills acts as a control.

**Metrics:** foreground image RMSE, depth error, maximum normal residual from `lod_error.csv`, triangles and GPU time.

**Success criterion:** feature-aware control improves ridge/cliff quality at similar cost more than it improves the smooth control scene. If it merely selects globally finer geometry everywhere, the hypothesis is not supported.

## H4 — temporal stability

**Independent variable:** hysteresis `0` versus `0.15` under an identical deterministic oscillating camera path.

**Metrics:** total LOD transitions, transitions/frame, image error at the sampled temporal state and GPU time.

**Success criterion:** hysteresis materially reduces unnecessary LOD transitions without causing a large persistent quality regression.

## H5 — null/equivalence test

The proposed path is forced to the finest LOD for all patches and compared with `reference` using the same shader and framebuffer configuration.

**Required result:** RGBA and depth readbacks must be byte-identical. If either differs, publication measurements are considered invalid until the harness discrepancy is explained.

## H6 — resolution response

At a fixed camera pose and method parameter, increasing viewport height increases the projected-error estimate because the same world-space residual covers more pixels.

**Test:** 720p, 1080p and 1440p resolution-scaling cases.

**Expected behavior:** error-controlled methods should tend to retain equal or finer representations as resolution increases. This is a method-behavior test, not a claim of performance superiority.

## Reporting rules

- Never report only the best run.
- Keep raw per-frame timings.
- Report absolute GPU time differences together with percentages.
- Keep outliers unless a documented measurement failure justifies exclusion.
- Do not call the projected estimator a proven strict screen-space bound unless a later derivation actually establishes that theorem.
- Do not use the finest mesh as "ground truth" without also reporting its analytic residual.
- Freeze the repository commit and `gfx-research-base` revision for final runs.
