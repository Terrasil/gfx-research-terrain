#version 460 core

layout(vertices = 4) out;

in VS_TESS {
    vec2 worldXZ;
} tcIn[];

out TC_TESS {
    vec2 worldXZ;
} tcOut[];

patch out float tcErrorRatio;
patch out float tcLevel;

uniform sampler2D uHeightMap;
uniform sampler2D uValidityMap;
uniform bool uUseValidityMask;
uniform mat4 uMvp;
uniform vec3 uEye;
uniform vec3 uLightDir;
uniform float uWorldSize;
uniform int uViewportWidth;
uniform int uViewportHeight;
uniform float uVerticalFovDegrees;
uniform float uErrorBudgetPx;
uniform float uFeatureWeight;
uniform float uNormalBudgetDegrees;
uniform float uRadianceBudget;
uniform float uScientificRoughness;
uniform int uDistanceLodBias;
uniform int uTessMethod;
uniform float uMaxTessLevel;
uniform bool uForceMaxTessellation;

const float PI = 3.14159265358979323846;

vec2 terrainUv(vec2 xz) {
    return clamp(xz / uWorldSize + vec2(0.5), vec2(0.0), vec2(1.0));
}

float terrainHeight(vec2 xz) {
    return textureLod(uHeightMap, terrainUv(xz), 0.0).r;
}

float terrainValidity(vec2 xz) {
    if (!uUseValidityMask) return 1.0;
    return textureLod(uValidityMap, terrainUv(xz), 0.0).r;
}

float patchValidity(vec2 p0, vec2 p1, vec2 p2, vec2 p3) {
    vec2 center = 0.25 * (p0 + p1 + p2 + p3);
    float v = max(max(terrainValidity(p0), terrainValidity(p1)), max(terrainValidity(p2), terrainValidity(p3)));
    v = max(v, terrainValidity(center));
    v = max(v, terrainValidity(0.5 * (p0 + p1)));
    v = max(v, terrainValidity(0.5 * (p1 + p2)));
    v = max(v, terrainValidity(0.5 * (p2 + p3)));
    v = max(v, terrainValidity(0.5 * (p3 + p0)));
    return v;
}

vec3 terrainPoint(vec2 xz) {
    return vec3(xz.x, terrainHeight(xz), xz.y);
}

float focalPixels() {
    float fov = radians(max(uVerticalFovDegrees, 1.0));
    return float(max(uViewportHeight, 1)) / max(2.0 * tan(0.5 * fov), 1.0e-5);
}

float conservativeProjectedPixels(float worldError, vec2 xz) {
    float distanceToEye = max(length(terrainPoint(xz) - uEye), 1.0e-3);
    return abs(worldError) * focalPixels() / distanceToEye;
}

vec2 clipToPixel(vec4 clipPosition) {
    vec2 viewport = vec2(float(max(uViewportWidth, 1)), float(max(uViewportHeight, 1)));
    vec2 ndc = clipPosition.xy / clipPosition.w;
    return (ndc * 0.5 + 0.5) * viewport;
}

float projectedPixels(float worldError, vec2 xz) {
    float errorMagnitude = abs(worldError);
    if (errorMagnitude <= 1.0e-8) return 0.0;

    vec3 worldPoint = terrainPoint(xz);
    vec4 baseClip = uMvp * vec4(worldPoint, 1.0);
    vec4 plusClip = uMvp * vec4(worldPoint + vec3(0.0, errorMagnitude, 0.0), 1.0);
    vec4 minusClip = uMvp * vec4(worldPoint - vec3(0.0, errorMagnitude, 0.0), 1.0);

    if (baseClip.w <= 1.0e-5 || plusClip.w <= 1.0e-5 || minusClip.w <= 1.0e-5) {
        return conservativeProjectedPixels(errorMagnitude, xz);
    }

    vec2 basePixel = clipToPixel(baseClip);
    float plusPixels = length(clipToPixel(plusClip) - basePixel);
    float minusPixels = length(clipToPixel(minusClip) - basePixel);
    return max(plusPixels, minusPixels);
}

vec3 smoothTerrainNormal(vec2 xz, float radius) {
    float r = max(radius, uWorldSize / 4096.0);
    float hx0 = terrainHeight(xz - vec2(r, 0.0));
    float hx1 = terrainHeight(xz + vec2(r, 0.0));
    float hz0 = terrainHeight(xz - vec2(0.0, r));
    float hz1 = terrainHeight(xz + vec2(0.0, r));
    return normalize(vec3(-(hx1 - hx0) / (2.0 * r), 1.0, -(hz1 - hz0) / (2.0 * r)));
}

float angleBetween(vec3 a, vec3 b) {
    return acos(clamp(dot(normalize(a), normalize(b)), -1.0, 1.0));
}

float edgeWorldResidual(vec2 a, vec2 b) {
    float ha = terrainHeight(a);
    float hb = terrainHeight(b);
    float maximumResidual = 0.0;
    const float samples[3] = float[3](0.25, 0.5, 0.75);
    for (int i = 0; i < 3; ++i) {
        float t = samples[i];
        vec2 p = mix(a, b, t);
        float linearHeight = mix(ha, hb, t);
        maximumResidual = max(maximumResidual, abs(terrainHeight(p) - linearHeight));
    }
    return maximumResidual;
}

float edgeResidualRms(vec2 a, vec2 b) {
    float ha = terrainHeight(a);
    float hb = terrainHeight(b);
    float sumSquares = 0.0;
    const float samples[5] = float[5](1.0 / 6.0, 2.0 / 6.0, 3.0 / 6.0, 4.0 / 6.0, 5.0 / 6.0);
    for (int i = 0; i < 5; ++i) {
        float t = samples[i];
        vec2 p = mix(a, b, t);
        float residual = terrainHeight(p) - mix(ha, hb, t);
        sumSquares += residual * residual;
    }
    return sqrt(sumSquares / 5.0);
}

float edgeNormalDeviation(vec2 a, vec2 b) {
    float radius = max(length(b - a) * 0.10, uWorldSize / 4096.0);
    vec3 n0 = smoothTerrainNormal(a, radius);
    vec3 n1 = smoothTerrainNormal(mix(a, b, 0.25), radius);
    vec3 n2 = smoothTerrainNormal(mix(a, b, 0.50), radius);
    vec3 n3 = smoothTerrainNormal(mix(a, b, 0.75), radius);
    vec3 n4 = smoothTerrainNormal(b, radius);
    vec3 meanNormal = normalize(n0 + n1 + n2 + n3 + n4);
    float maximumAngle = 0.0;
    maximumAngle = max(maximumAngle, angleBetween(meanNormal, n0));
    maximumAngle = max(maximumAngle, angleBetween(meanNormal, n1));
    maximumAngle = max(maximumAngle, angleBetween(meanNormal, n2));
    maximumAngle = max(maximumAngle, angleBetween(meanNormal, n3));
    maximumAngle = max(maximumAngle, angleBetween(meanNormal, n4));
    return maximumAngle;
}

float patchBilinearHeight(vec2 p0, vec2 p1, vec2 p2, vec2 p3, float u, float v) {
    float bottom = mix(terrainHeight(p0), terrainHeight(p1), u);
    float top = mix(terrainHeight(p3), terrainHeight(p2), u);
    return mix(bottom, top, v);
}

float patchInteriorResidual(vec2 p0, vec2 p1, vec2 p2, vec2 p3) {
    const vec2 uvSamples[5] = vec2[5](
        vec2(0.50, 0.50),
        vec2(0.25, 0.25),
        vec2(0.75, 0.25),
        vec2(0.25, 0.75),
        vec2(0.75, 0.75)
    );
    float maximumResidual = 0.0;
    for (int i = 0; i < 5; ++i) {
        float u = uvSamples[i].x;
        float v = uvSamples[i].y;
        vec2 bottom = mix(p0, p1, u);
        vec2 top = mix(p3, p2, u);
        vec2 xz = mix(bottom, top, v);
        float residual = abs(terrainHeight(xz) - patchBilinearHeight(p0, p1, p2, p3, u, v));
        maximumResidual = max(maximumResidual, residual);
    }
    return maximumResidual;
}

float patchInteriorResidualRms(vec2 p0, vec2 p1, vec2 p2, vec2 p3) {
    const vec2 uvSamples[9] = vec2[9](
        vec2(0.25, 0.25), vec2(0.50, 0.25), vec2(0.75, 0.25),
        vec2(0.25, 0.50), vec2(0.50, 0.50), vec2(0.75, 0.50),
        vec2(0.25, 0.75), vec2(0.50, 0.75), vec2(0.75, 0.75));
    float sumSquares = 0.0;
    for (int i = 0; i < 9; ++i) {
        float u = uvSamples[i].x;
        float v = uvSamples[i].y;
        vec2 bottom = mix(p0, p1, u);
        vec2 top = mix(p3, p2, u);
        vec2 xz = mix(bottom, top, v);
        float residual = terrainHeight(xz) - patchBilinearHeight(p0, p1, p2, p3, u, v);
        sumSquares += residual * residual;
    }
    return sqrt(sumSquares / 9.0);
}

float patchInteriorNormalDeviation(vec2 p0, vec2 p1, vec2 p2, vec2 p3) {
    vec2 center = 0.25 * (p0 + p1 + p2 + p3);
    float radius = max(length(p2 - p0) * 0.06, uWorldSize / 4096.0);
    vec3 nc = smoothTerrainNormal(center, radius);
    float maximumAngle = 0.0;
    const vec2 uvSamples[4] = vec2[4](
        vec2(0.25, 0.25), vec2(0.75, 0.25), vec2(0.25, 0.75), vec2(0.75, 0.75));
    for (int i = 0; i < 4; ++i) {
        vec2 bottom = mix(p0, p1, uvSamples[i].x);
        vec2 top = mix(p3, p2, uvSamples[i].x);
        vec2 xz = mix(bottom, top, uvSamples[i].y);
        maximumAngle = max(maximumAngle, angleBetween(nc, smoothTerrainNormal(xz, radius)));
    }
    return maximumAngle;
}

float edgeFeatureStrength(vec2 a, vec2 b) {
    return clamp(edgeNormalDeviation(a, b) / radians(45.0), 0.0, 2.0);
}

float geometricRatio(vec2 a, vec2 b) {
    vec2 midpoint = 0.5 * (a + b);
    return projectedPixels(edgeWorldResidual(a, b), midpoint) / max(uErrorBudgetPx, 1.0e-4);
}

float varianceRatio(vec2 a, vec2 b) {
    vec2 midpoint = 0.5 * (a + b);
    return projectedPixels(2.0 * edgeResidualRms(a, b), midpoint) / max(uErrorBudgetPx, 1.0e-4);
}

float normalBoundRatio(vec2 a, vec2 b) {
    float budget = radians(max(uNormalBudgetDegrees, 0.1));
    return edgeNormalDeviation(a, b) / budget;
}

float ggxDistribution(float nDotH, float roughness) {
    float a = max(roughness * roughness, 0.0025);
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1.0e-5);
}

float schlickGeometry(float nDotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotX / max(nDotX * (1.0 - k) + k, 1.0e-5);
}

float scientificLuminance(vec3 n, vec3 worldPoint) {
    vec3 l = normalize(-uLightDir);
    vec3 v = normalize(uEye - worldPoint);
    vec3 h = normalize(l + v);
    float nDotL = max(dot(n, l), 0.0);
    float nDotV = max(dot(n, v), 0.0);
    float nDotH = max(dot(n, h), 0.0);
    float vDotH = max(dot(v, h), 0.0);
    float roughness = clamp(uScientificRoughness, 0.08, 1.0);
    float f0 = 0.04;
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
    float specular = ggxDistribution(nDotH, roughness)
        * schlickGeometry(nDotV, roughness)
        * schlickGeometry(nDotL, roughness)
        * fresnel / max(4.0 * nDotV * nDotL, 1.0e-4);
    float diffuse = 0.48 / PI;
    return (diffuse + specular) * nDotL * 2.6 + 0.08;
}

float localNormalSensitivity(vec2 xz, float radius) {
    vec3 p = terrainPoint(xz);
    vec3 n = smoothTerrainNormal(xz, radius);
    vec3 axis = abs(n.y) < 0.92 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(axis, n));
    vec3 bitangent = normalize(cross(n, tangent));
    float stepAngle = radians(1.0);
    float c = cos(stepAngle);
    float s = sin(stepAngle);
    float base = scientificLuminance(n, p);
    float maximumDelta = 0.0;
    maximumDelta = max(maximumDelta, abs(scientificLuminance(normalize(n * c + tangent * s), p) - base));
    maximumDelta = max(maximumDelta, abs(scientificLuminance(normalize(n * c - tangent * s), p) - base));
    maximumDelta = max(maximumDelta, abs(scientificLuminance(normalize(n * c + bitangent * s), p) - base));
    maximumDelta = max(maximumDelta, abs(scientificLuminance(normalize(n * c - bitangent * s), p) - base));
    return maximumDelta / stepAngle;
}

float contextRadianceRatio(vec2 a, vec2 b) {
    vec2 midpoint = 0.5 * (a + b);
    float radius = max(length(b - a) * 0.10, uWorldSize / 4096.0);
    float predictedRadianceError = localNormalSensitivity(midpoint, radius) * edgeNormalDeviation(a, b);
    return predictedRadianceError / max(uRadianceBudget, 1.0e-5);
}

float distanceFactor(vec2 a, vec2 b) {
    vec2 midpoint = 0.5 * (a + b);
    float distanceToEye = max(length(terrainPoint(midpoint) - uEye), 1.0e-3);
    float edgePixels = length(b - a) * focalPixels() / distanceToEye;
    float targetPixels = 26.0 * exp2(float(uDistanceLodBias));
    return clamp(edgePixels / max(targetPixels, 2.0), 1.0, uMaxTessLevel);
}

float factorFromGeometricRatio(float ratio) {
    return clamp(1.15 * sqrt(max(ratio, 0.0)), 1.0, uMaxTessLevel);
}

float factorFromFirstOrderRatio(float ratio) {
    return clamp(1.05 * max(ratio, 0.0), 1.0, uMaxTessLevel);
}

float controlledEdgeRatio(vec2 a, vec2 b) {
    float g = geometricRatio(a, b);
    if (uTessMethod == 1) return g;
    if (uTessMethod == 2) return g * (1.0 + uFeatureWeight * edgeFeatureStrength(a, b));
    if (uTessMethod == 3) return varianceRatio(a, b);
    if (uTessMethod == 4) return max(g, normalBoundRatio(a, b));
    if (uTessMethod == 5) {
        return max(g, max(normalBoundRatio(a, b), contextRadianceRatio(a, b)));
    }
    return g;
}

float controlledEdgeFactor(vec2 a, vec2 b) {
    float g = geometricRatio(a, b);
    if (uTessMethod == 1) return factorFromGeometricRatio(g);
    if (uTessMethod == 2) {
        float legacy = g * (1.0 + uFeatureWeight * edgeFeatureStrength(a, b));
        return factorFromGeometricRatio(legacy);
    }
    if (uTessMethod == 3) return factorFromGeometricRatio(varianceRatio(a, b));

    float geometryFactor = factorFromGeometricRatio(g);
    float normalFactor = factorFromFirstOrderRatio(normalBoundRatio(a, b));
    if (uTessMethod == 4) return max(geometryFactor, normalFactor);
    if (uTessMethod == 5) {
        float radianceFactor = factorFromFirstOrderRatio(contextRadianceRatio(a, b));
        return max(geometryFactor, max(normalFactor, radianceFactor));
    }
    return geometryFactor;
}

float controlledInteriorRatio(vec2 p0, vec2 p1, vec2 p2, vec2 p3) {
    vec2 center = 0.25 * (p0 + p1 + p2 + p3);
    float g = projectedPixels(patchInteriorResidual(p0, p1, p2, p3), center)
        / max(uErrorBudgetPx, 1.0e-4);
    if (uTessMethod == 3) {
        return projectedPixels(2.0 * patchInteriorResidualRms(p0, p1, p2, p3), center)
            / max(uErrorBudgetPx, 1.0e-4);
    }
    float normalRatio = patchInteriorNormalDeviation(p0, p1, p2, p3)
        / radians(max(uNormalBudgetDegrees, 0.1));
    if (uTessMethod == 4) return max(g, normalRatio);
    if (uTessMethod == 5) {
        float radius = max(length(p2 - p0) * 0.06, uWorldSize / 4096.0);
        float predicted = localNormalSensitivity(center, radius)
            * patchInteriorNormalDeviation(p0, p1, p2, p3);
        float radianceRatio = predicted / max(uRadianceBudget, 1.0e-5);
        return max(g, max(normalRatio, radianceRatio));
    }
    return g;
}

float controlledInteriorFactor(vec2 p0, vec2 p1, vec2 p2, vec2 p3) {
    vec2 center = 0.25 * (p0 + p1 + p2 + p3);
    float g = projectedPixels(patchInteriorResidual(p0, p1, p2, p3), center)
        / max(uErrorBudgetPx, 1.0e-4);
    if (uTessMethod == 3) {
        float variance = projectedPixels(2.0 * patchInteriorResidualRms(p0, p1, p2, p3), center)
            / max(uErrorBudgetPx, 1.0e-4);
        return factorFromGeometricRatio(variance);
    }
    float geometryFactor = factorFromGeometricRatio(g);
    if (uTessMethod < 4) return geometryFactor;

    float normalDeviation = patchInteriorNormalDeviation(p0, p1, p2, p3);
    float normalRatio = normalDeviation / radians(max(uNormalBudgetDegrees, 0.1));
    float normalFactor = factorFromFirstOrderRatio(normalRatio);
    if (uTessMethod == 4) return max(geometryFactor, normalFactor);

    float radius = max(length(p2 - p0) * 0.06, uWorldSize / 4096.0);
    float predicted = localNormalSensitivity(center, radius) * normalDeviation;
    float radianceRatio = predicted / max(uRadianceBudget, 1.0e-5);
    float radianceFactor = factorFromFirstOrderRatio(radianceRatio);
    return max(geometryFactor, max(normalFactor, radianceFactor));
}

void main() {
    tcOut[gl_InvocationID].worldXZ = tcIn[gl_InvocationID].worldXZ;
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    barrier();
    if (gl_InvocationID != 0) return;

    vec2 p0 = tcIn[0].worldXZ;
    vec2 p1 = tcIn[1].worldXZ;
    vec2 p2 = tcIn[2].worldXZ;
    vec2 p3 = tcIn[3].worldXZ;

    if (patchValidity(p0, p1, p2, p3) < 0.5) {
        gl_TessLevelOuter[0] = 0.0;
        gl_TessLevelOuter[1] = 0.0;
        gl_TessLevelOuter[2] = 0.0;
        gl_TessLevelOuter[3] = 0.0;
        gl_TessLevelInner[0] = 0.0;
        gl_TessLevelInner[1] = 0.0;
        tcLevel = 0.0;
        tcErrorRatio = 0.0;
        return;
    }

    if (uForceMaxTessellation) {
        float level = max(uMaxTessLevel, 1.0);
        gl_TessLevelOuter[0] = level;
        gl_TessLevelOuter[1] = level;
        gl_TessLevelOuter[2] = level;
        gl_TessLevelOuter[3] = level;
        gl_TessLevelInner[0] = level;
        gl_TessLevelInner[1] = level;
        tcLevel = log2(level);
        tcErrorRatio = 0.0;
        return;
    }

    float leftRatio = 0.0;
    float bottomRatio = 0.0;
    float rightRatio = 0.0;
    float topRatio = 0.0;
    float leftFactor;
    float bottomFactor;
    float rightFactor;
    float topFactor;

    if (uTessMethod == 0) {
        leftFactor = distanceFactor(p0, p3);
        bottomFactor = distanceFactor(p0, p1);
        rightFactor = distanceFactor(p1, p2);
        topFactor = distanceFactor(p3, p2);
    } else {
        leftRatio = controlledEdgeRatio(p0, p3);
        bottomRatio = controlledEdgeRatio(p0, p1);
        rightRatio = controlledEdgeRatio(p1, p2);
        topRatio = controlledEdgeRatio(p3, p2);
        leftFactor = controlledEdgeFactor(p0, p3);
        bottomFactor = controlledEdgeFactor(p0, p1);
        rightFactor = controlledEdgeFactor(p1, p2);
        topFactor = controlledEdgeFactor(p3, p2);
    }

    gl_TessLevelOuter[0] = leftFactor;
    gl_TessLevelOuter[1] = bottomFactor;
    gl_TessLevelOuter[2] = rightFactor;
    gl_TessLevelOuter[3] = topFactor;

    float interiorRatio = uTessMethod == 0 ? 0.0 : controlledInteriorRatio(p0, p1, p2, p3);
    float interiorFactor = uTessMethod == 0 ? 1.0 : controlledInteriorFactor(p0, p1, p2, p3);
    gl_TessLevelInner[0] = max(max(bottomFactor, topFactor), interiorFactor);
    gl_TessLevelInner[1] = max(max(leftFactor, rightFactor), interiorFactor);

    float maximumFactor = max(max(leftFactor, rightFactor), max(bottomFactor, topFactor));
    maximumFactor = max(maximumFactor, interiorFactor);
    tcLevel = log2(max(maximumFactor, 1.0));
    tcErrorRatio = max(max(leftRatio, rightRatio), max(bottomRatio, topRatio));
    tcErrorRatio = max(tcErrorRatio, interiorRatio);
}
