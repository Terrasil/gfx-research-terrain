#version 460 core

uniform int uTessPatchGrid;
uniform float uWorldSize;

out VS_TESS {
    vec2 worldXZ;
} vs;

void main() {
    int patchIndex = gl_VertexID / 4;
    int corner = gl_VertexID & 3;
    int px = patchIndex % uTessPatchGrid;
    int pz = patchIndex / uTessPatchGrid;

    const vec2 corners[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );

    vec2 uv = (vec2(px, pz) + corners[corner]) / float(uTessPatchGrid);
    vs.worldXZ = (uv - vec2(0.5)) * uWorldSize;
    gl_Position = vec4(vs.worldXZ.x, 0.0, vs.worldXZ.y, 1.0);
}
