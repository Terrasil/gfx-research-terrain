#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uMvp;
uniform int uScene;
uniform int uCurrentCells;
uniform vec4 uPatchBounds;   // minX, maxX, minZ, maxZ
uniform vec4 uNeighborCells; // left, right, bottom, top

out VS_OUT {
    vec3 worldPosition;
    vec3 normal;
    vec2 uv;
} vs;

float terrainHeight(vec2 p) {
    float x = p.x;
    float z = p.y;
    if (uScene == 0) {
        return 0.65 * sin(0.55 * x) * cos(0.48 * z) +
               0.18 * sin(1.25 * x + 0.4 * z);
    }
    if (uScene == 1) {
        float ridge = 0.9 * exp(-7.5 * abs(z - 0.25 * sin(0.6 * x)));
        return 0.25 * sin(0.45 * x) + ridge;
    }
    if (uScene == 2) {
        float low = 0.55 * sin(0.38 * x) * cos(0.44 * z);
        float medium = 0.20 * sin(1.7 * x + 0.2) * sin(1.35 * z);
        float high = 0.055 * sin(6.5 * x + 1.7 * z);
        float rock = 0.7 * exp(-0.32 * ((x - 2.1) * (x - 2.1) + (z + 1.4) * (z + 1.4)));
        return low + medium + high + rock;
    }
    float band = 0.85 * tanh(8.0 * (z + 0.2 * sin(0.7 * x)));
    return 0.22 * sin(0.55 * x) + 0.35 * band;
}

float horizontalEdgeHeight(float z, float t, int cells) {
    float scaled = clamp(t, 0.0, 1.0) * float(cells);
    int segment = min(int(floor(scaled)), cells - 1);
    float f = scaled - float(segment);
    float x0 = mix(uPatchBounds.x, uPatchBounds.y, float(segment) / float(cells));
    float x1 = mix(uPatchBounds.x, uPatchBounds.y, float(segment + 1) / float(cells));
    return mix(terrainHeight(vec2(x0, z)), terrainHeight(vec2(x1, z)), f);
}

float verticalEdgeHeight(float x, float t, int cells) {
    float scaled = clamp(t, 0.0, 1.0) * float(cells);
    int segment = min(int(floor(scaled)), cells - 1);
    float f = scaled - float(segment);
    float z0 = mix(uPatchBounds.z, uPatchBounds.w, float(segment) / float(cells));
    float z1 = mix(uPatchBounds.z, uPatchBounds.w, float(segment + 1) / float(cells));
    return mix(terrainHeight(vec2(x, z0)), terrainHeight(vec2(x, z1)), f);
}

void main() {
    vec3 position = aPosition;
    const float edgeEpsilon = 1.0e-6;

    int leftCells = max(1, int(uNeighborCells.x + 0.5));
    int rightCells = max(1, int(uNeighborCells.y + 0.5));
    int bottomCells = max(1, int(uNeighborCells.z + 0.5));
    int topCells = max(1, int(uNeighborCells.w + 0.5));

    // A fine patch snaps only its shared edge to the coarser neighbor's
    // piecewise-linear boundary. This removes T-junction cracks without
    // changing the interior LOD selected by the controller.
    if (aUv.x <= edgeEpsilon && leftCells < uCurrentCells) {
        position.y = verticalEdgeHeight(uPatchBounds.x, aUv.y, leftCells);
    }
    if (aUv.x >= 1.0 - edgeEpsilon && rightCells < uCurrentCells) {
        position.y = verticalEdgeHeight(uPatchBounds.y, aUv.y, rightCells);
    }
    if (aUv.y <= edgeEpsilon && bottomCells < uCurrentCells) {
        position.y = horizontalEdgeHeight(uPatchBounds.z, aUv.x, bottomCells);
    }
    if (aUv.y >= 1.0 - edgeEpsilon && topCells < uCurrentCells) {
        position.y = horizontalEdgeHeight(uPatchBounds.w, aUv.x, topCells);
    }

    vs.worldPosition = position;
    vs.normal = normalize(aNormal);
    vs.uv = aUv;
    gl_Position = uMvp * vec4(position, 1.0);
}
