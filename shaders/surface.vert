#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in float aAdaptiveLevel;
layout(location = 4) in float aAdaptiveErrorRatio;

uniform mat4 uMvp;
uniform int uScene;
uniform int uCurrentCells;
uniform vec4 uPatchBounds;   // minX, maxX, minZ, maxZ
uniform vec4 uNeighborCells; // left, right, bottom, top
uniform bool uUseHeightMap;
uniform bool uAdaptiveMesh;
uniform sampler2D uHeightMap;
uniform float uWorldSize;
uniform float uHeightMin;
uniform float uHeightMax;
uniform int uRenderMode;
uniform int uLod;
uniform float uErrorRatio;

uniform sampler2D uGrassHeight;
uniform sampler2D uDirtHeight;
uniform sampler2D uRockHeight;
uniform sampler2D uSandHeight;

out VS_OUT {
    vec3 worldPosition;
    vec3 normal;
    vec2 uv;
    float lodLevel;
    float errorRatio;
    float validity;
} vs;

float terrainHeight(vec2 p) {
    if (uUseHeightMap) {
        vec2 uv = clamp(p / uWorldSize + vec2(0.5), vec2(0.0), vec2(1.0));
        return textureLod(uHeightMap, uv, 0.0).r;
    }

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

vec3 triWeights(vec3 n) {
    vec3 w = pow(abs(n), vec3(5.0));
    return w / max(w.x + w.y + w.z, 1.0e-5);
}

float triHeight(sampler2D tex, vec3 p, vec3 n, float scale) {
    vec3 w = triWeights(n);
    float x = textureLod(tex, p.zy / scale, 0.0).r;
    float y = textureLod(tex, p.xz / scale, 0.0).r;
    float z = textureLod(tex, p.xy / scale, 0.0).r;
    return x * w.x + y * w.y + z * w.z;
}

vec4 materialWeights(vec3 n, float heightN) {
    float slope = 1.0 - clamp(n.y, 0.0, 1.0);
    float sand = (1.0 - smoothstep(0.16, 0.28, heightN)) * (1.0 - smoothstep(0.28, 0.52, slope));
    float rock = smoothstep(0.22, 0.52, slope);
    rock += 0.22 * smoothstep(0.58, 0.90, heightN) * smoothstep(0.12, 0.42, slope);
    float grass = smoothstep(0.10, 0.28, heightN) * (1.0 - smoothstep(0.18, 0.42, slope));
    float dirt = max(0.10, 1.0 - sand - rock - grass);
    vec4 weights = max(vec4(grass, dirt, rock, sand), vec4(0.0));
    return weights / max(dot(weights, vec4(1.0)), 1.0e-5);
}

float realisticMicroDisplacement(vec3 p, vec3 n) {
    float heightN = clamp((p.y - uHeightMin) / max(uHeightMax - uHeightMin, 1.0e-4), 0.0, 1.0);
    vec4 mw = materialWeights(n, heightN);
    float grass = triHeight(uGrassHeight, p, n, 2.0) - 0.5;
    float dirt = triHeight(uDirtHeight, p, n, 2.1) - 0.5;
    float rock = triHeight(uRockHeight, p, n, 1.5) - 0.5;
    float sand = triHeight(uSandHeight, p, n, 2.0) - 0.5;
    return grass * mw.x * 0.030 + dirt * mw.y * 0.040 + rock * mw.z * 0.115 + sand * mw.w * 0.025;
}

void main() {
    vec3 position = aPosition;
    const float edgeEpsilon = 1.0e-6;

    if (!uAdaptiveMesh) {
        int leftCells = max(1, int(uNeighborCells.x + 0.5));
        int rightCells = max(1, int(uNeighborCells.y + 0.5));
        int bottomCells = max(1, int(uNeighborCells.z + 0.5));
        int topCells = max(1, int(uNeighborCells.w + 0.5));

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
    }

    // Height textures are intentionally not used for vertex displacement here. Adaptive LOD
    // boundaries contain T-junction stitch vertices, so nonlinear per-vertex displacement would
    // re-open cracks even when the base terrain topology is watertight. Relief stays in shading.

    vs.worldPosition = position;
    vs.normal = normalize(aNormal);
    vs.uv = aUv;
    vs.lodLevel = uAdaptiveMesh ? aAdaptiveLevel : float(uLod);
    vs.errorRatio = uAdaptiveMesh ? aAdaptiveErrorRatio : uErrorRatio;
    vs.validity = 1.0;
    gl_Position = uMvp * vec4(position, 1.0);
}
