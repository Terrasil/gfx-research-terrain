# Implemented theory

This file describes the equations that the current code actually implements. It is intentionally narrower than the doctoral thesis.

## 1. Discrete representations

Each terrain patch has nested regular-grid meshes

\[
R_{i,0}, R_{i,1}, \ldots, R_{i,K-1},
\]

where larger `lod` means a denser mesh. For each representation the preprocessing stage samples the same analytic heightfield densely and records:

- maximum absolute height residual \(e^h_{i,k}\);
- RMS height residual;
- maximum normal deviation \(\theta_{i,k}\);
- maximum and RMS local feature strength;
- triangle count.

The finest mesh is the rendering reference, but its residual against the analytic source is also recorded. It is not silently treated as mathematically exact.

## 2. Projected geometric estimator

For a patch with bounding sphere radius \(r_i\), camera-to-centre distance \(d_i\), viewport height \(H\), and vertical field of view \(\phi\), the implementation uses

\[
d_i^- = \max(0.05, d_i-r_i),
\]

\[
f_{px}=\frac{H}{2\tan(\phi/2)},
\]

\[
\widehat E^{geom}_{i,k}
= e^h_{i,k}\frac{f_{px}}{d_i^-}.
\]

Using the nearest bounding-sphere distance makes the estimator more conservative than using patch-centre distance alone. It is still an estimator, not a formal proof that every rasterized screen-space displacement is bounded by the requested threshold. The benchmark explicitly measures rendered error to test calibration.

## 3. Feature-aware controller

The feature-aware variant adds two preregistered terms. Let \(F_{i,k}\in[0,1]\) be maximum feature strength and \(\theta_{i,k}\) maximum normal error. A normal-displacement proxy is

\[
E^n_{i,k}
= r_i\sin(\min(\theta_{i,k},90^\circ))\frac{f_{px}}{d_i^-}.
\]

The controller uses

\[
\widehat E^{feature}_{i,k}
= \widehat E^{geom}_{i,k}
+ w_f\left(0.25F_{i,k}\widehat E^{geom}_{i,k}+0.05E^n_{i,k}\right).
\]

The coefficients `0.25` and `0.05` are part of the method under test. They must not be retuned after seeing publication results without reporting a separate tuning experiment.

## 4. LOD decision

The error-controlled methods choose the coarsest representation satisfying

\[
\widehat E_{i,k}\leq\varepsilon_{px}.
\]

If no level satisfies the threshold, the finest available level is selected.

The distance baseline does not use the error estimator. It uses a conventional logarithmic distance rule and sweeps an integer bias so the baseline forms a quality/cost curve rather than a single arbitrary operating point.

## 5. Hysteresis

For the adaptive controller, refinement occurs only when the previous representation exceeds

\[
\varepsilon_{px}(1+h),
\]

and coarsening occurs only when the candidate is below

\[
\varepsilon_{px}(1-h).
\]

The temporal experiment compares `h=0` with a preregistered non-zero value while the camera follows the same deterministic motion.

## 6. Boundary consistency

If adjacent patches use different resolutions, the finer patch contains additional boundary vertices. Those vertices are snapped in the vertex shader to the coarser neighbour's piecewise-linear edge. The operation changes only shared boundary positions; it does not increase the interior LOD.

This is an experimental control against T-junction cracks. The current implementation does not recompute normals after the boundary snap, so a small boundary-shading residual may remain and must be acknowledged if visible in the data.

## 7. Metrics

The harness records three different layers of evidence:

1. **representation error** against the analytic source (`lod_error.csv`);
2. **rendered quality error** against the finest-mesh reference (`quality.csv`);
3. **cost/work** through GPU timestamps, triangles and LOD distributions (`raw.csv`).

No one metric is treated as a substitute for the others.
