#version 460 core

layout(quads, fractional_even_spacing, cw) in;

in TC_TESS {
    vec2 worldXZ;
} teIn[];

patch in float tcErrorRatio;
patch in float tcLevel;

uniform mat4 uMvp;
uniform sampler2D uHeightMap;
uniform sampler2D uValidityMap;
uniform bool uUseValidityMask;
uniform float uWorldSize;

out VS_OUT {
    vec3 worldPosition;
    vec3 normal;
    vec2 uv;
    float lodLevel;
    float errorRatio;
    float validity;
} vs;

vec2 terrainUv(vec2 xz) {
    return clamp(xz / uWorldSize + vec2(0.5), vec2(0.0), vec2(1.0));
}

float terrainHeight(vec2 xz) {
    return textureLod(uHeightMap, terrainUv(xz), 0.0).r;
}

vec3 terrainNormal(vec2 xz) {
    ivec2 dimensions = textureSize(uHeightMap, 0);
    float texelWorld = uWorldSize / max(float(max(dimensions.x, dimensions.y) - 1), 1.0);
    float hLeft = terrainHeight(xz - vec2(texelWorld, 0.0));
    float hRight = terrainHeight(xz + vec2(texelWorld, 0.0));
    float hBottom = terrainHeight(xz - vec2(0.0, texelWorld));
    float hTop = terrainHeight(xz + vec2(0.0, texelWorld));
    float dx = (hRight - hLeft) / max(2.0 * texelWorld, 1.0e-5);
    float dz = (hTop - hBottom) / max(2.0 * texelWorld, 1.0e-5);
    return normalize(vec3(-dx, 1.0, -dz));
}

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;
    vec2 bottom = mix(teIn[0].worldXZ, teIn[1].worldXZ, u);
    vec2 top = mix(teIn[3].worldXZ, teIn[2].worldXZ, u);
    vec2 xz = mix(bottom, top, v);
    float y = terrainHeight(xz);

    vs.worldPosition = vec3(xz.x, y, xz.y);
    vs.normal = terrainNormal(xz);
    vs.uv = terrainUv(xz);
    vs.lodLevel = tcLevel;
    vs.errorRatio = tcErrorRatio;
    vs.validity = uUseValidityMask ? textureLod(uValidityMap, vs.uv, 0.0).r : 1.0;
    gl_Position = uMvp * vec4(vs.worldPosition, 1.0);
}
