#version 460 core

in VS_OUT {
    vec3 worldPosition;
    vec3 normal;
    vec2 uv;
    float lodLevel;
    float errorRatio;
    float validity;
} fs;

layout(location = 0) out vec4 outColor;

uniform vec3 uEye;
uniform vec3 uLightDir;
uniform int uRenderMode; // 0 scientific, 1 realistic, 2 LOD, 3 error, 4 normal variation, 5 budget margin, 6 slope
uniform int uScene;
uniform float uHeightMin;
uniform float uHeightMax;
uniform float uWorldSize;
uniform float uScientificRoughness;
uniform int uShadingNormalMode; // 0 auto, 1 geometric, 2 smooth

uniform sampler2D uGrassDiffuse;
uniform sampler2D uGrassNormal;
uniform sampler2D uGrassRoughness;
uniform sampler2D uGrassHeight;
uniform sampler2D uDirtDiffuse;
uniform sampler2D uDirtNormal;
uniform sampler2D uDirtRoughness;
uniform sampler2D uDirtHeight;
uniform sampler2D uRockDiffuse;
uniform sampler2D uRockNormal;
uniform sampler2D uRockRoughness;
uniform sampler2D uRockHeight;
uniform sampler2D uSandDiffuse;
uniform sampler2D uSandNormal;
uniform sampler2D uSandRoughness;
uniform sampler2D uSandHeight;

const float PI = 3.14159265358979323846;

vec3 lodColor(float lod) {
    const vec3 colors[10] = vec3[10](
        vec3(0.84, 0.10, 0.10),
        vec3(0.95, 0.33, 0.08),
        vec3(0.96, 0.66, 0.05),
        vec3(0.70, 0.82, 0.10),
        vec3(0.20, 0.76, 0.26),
        vec3(0.08, 0.70, 0.66),
        vec3(0.10, 0.45, 0.92),
        vec3(0.36, 0.25, 0.92),
        vec3(0.67, 0.18, 0.85),
        vec3(0.92, 0.12, 0.62)
    );
    int index = clamp(int(floor(lod + 0.5)), 0, 9);
    return colors[index];
}

vec3 errorColor(float ratio) {
    if (ratio <= 0.5) return mix(vec3(0.05, 0.24, 0.75), vec3(0.12, 0.75, 0.31), ratio / 0.5);
    if (ratio <= 1.0) return mix(vec3(0.12, 0.75, 0.31), vec3(0.96, 0.78, 0.08), (ratio - 0.5) / 0.5);
    if (ratio <= 1.5) return mix(vec3(0.96, 0.78, 0.08), vec3(0.95, 0.15, 0.06), (ratio - 1.0) / 0.5);
    return mix(vec3(0.95, 0.15, 0.06), vec3(0.92, 0.05, 0.72), clamp((ratio - 1.5) / 1.5, 0.0, 1.0));
}

vec3 triWeights(vec3 n) {
    vec3 w = pow(abs(n), vec3(5.0));
    return w / max(w.x + w.y + w.z, 1.0e-5);
}

vec3 triColor(sampler2D tex, vec3 p, vec3 n, float scale) {
    vec3 w = triWeights(n);
    vec3 cx = texture(tex, p.zy / scale).rgb;
    vec3 cy = texture(tex, p.xz / scale).rgb;
    vec3 cz = texture(tex, p.xy / scale).rgb;
    return cx * w.x + cy * w.y + cz * w.z;
}

float triScalar(sampler2D tex, vec3 p, vec3 n, float scale) {
    vec3 w = triWeights(n);
    float x = texture(tex, p.zy / scale).r;
    float y = texture(tex, p.xz / scale).r;
    float z = texture(tex, p.xy / scale).r;
    return x * w.x + y * w.y + z * w.z;
}

vec3 tangentNormal(sampler2D tex, vec2 uv) {
    return normalize(texture(tex, uv).xyz * 2.0 - 1.0);
}

vec3 triNormal(sampler2D tex, vec3 p, vec3 geometricNormal, float scale) {
    vec3 w = triWeights(geometricNormal);
    vec3 tx = tangentNormal(tex, p.zy / scale);
    vec3 ty = tangentNormal(tex, p.xz / scale);
    vec3 tz = tangentNormal(tex, p.xy / scale);

    vec3 nx = normalize(vec3(tx.z * sign(geometricNormal.x), tx.y, tx.x));
    vec3 ny = normalize(vec3(ty.x, ty.z * sign(geometricNormal.y), ty.y));
    vec3 nz = normalize(vec3(tz.x, tz.y, tz.z * sign(geometricNormal.z)));
    return normalize(nx * w.x + ny * w.y + nz * w.z);
}

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float terrainNoise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float terrainFbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += amplitude * terrainNoise2D(p);
        p = mat2(1.61, 1.17, -1.17, 1.61) * p;
        amplitude *= 0.5;
    }
    return value;
}

void baseMaterialWeights(vec3 n, float heightN, out vec4 weights) {
    float slope = 1.0 - clamp(n.y, 0.0, 1.0);
    float sand = (1.0 - smoothstep(0.15, 0.27, heightN)) * (1.0 - smoothstep(0.28, 0.52, slope));
    float rock = smoothstep(0.20, 0.48, slope);
    rock += 0.25 * smoothstep(0.58, 0.90, heightN) * smoothstep(0.10, 0.40, slope);
    float grass = smoothstep(0.10, 0.27, heightN) * (1.0 - smoothstep(0.18, 0.40, slope));
    float dirt = max(0.10, 1.0 - sand - rock - grass);
    weights = max(vec4(grass, dirt, rock, sand), vec4(0.0));
    weights /= max(dot(weights, vec4(1.0)), 1.0e-5);
}

float snowWeight(vec3 n, float heightN) {
    float sceneFactor = (uScene == 4 || uScene == 6 || uScene == 7 || uScene == 10 || uScene == 11) ? 1.0 : 0.0;
    float slope = 1.0 - clamp(n.y, 0.0, 1.0);
    float altitude = smoothstep(0.62, 0.80, heightN);
    float slopeRetention = 1.0 - smoothstep(0.28, 0.64, slope);
    float breakup = mix(0.72, 1.12, terrainFbm(fs.worldPosition.xz * 0.55 + vec2(7.0, -3.0)));
    return clamp(sceneFactor * altitude * slopeRetention * breakup, 0.0, 0.96);
}

float D_GGX(float nDotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1.0e-5);
}

float G_Schlick(float nDotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 1.0e-5);
}

vec3 F_Schlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

vec3 shadePbr(vec3 albedo, vec3 n, float roughness, float cavity) {
    vec3 l = normalize(-uLightDir);
    vec3 v = normalize(uEye - fs.worldPosition);
    vec3 h = normalize(l + v);
    float nDotL = max(dot(n, l), 0.0);
    float nDotV = max(dot(n, v), 0.0);
    float nDotH = max(dot(n, h), 0.0);
    float vDotH = max(dot(v, h), 0.0);

    vec3 f0 = vec3(0.035);
    vec3 F = F_Schlick(vDotH, f0);
    float D = D_GGX(nDotH, max(roughness, 0.08));
    float G = G_Schlick(nDotV, roughness) * G_Schlick(nDotL, roughness);
    vec3 specular = (D * G * F) / max(4.0 * nDotV * nDotL, 1.0e-4);
    vec3 diffuse = (vec3(1.0) - F) * albedo / PI;

    vec3 sunColor = vec3(1.0, 0.91, 0.77) * 3.05;
    vec3 skyColor = vec3(0.30, 0.40, 0.55);
    float skyAmount = 0.13 + 0.17 * clamp(n.y, 0.0, 1.0);
    vec3 ambient = albedo * skyColor * skyAmount;
    vec3 bounce = albedo * vec3(0.10, 0.075, 0.052) * clamp(n.y, 0.0, 1.0);
    return ((diffuse + specular) * sunColor * nDotL + ambient + bounce) * cavity;
}

vec3 scientificShading(vec3 n) {
    vec3 l = normalize(-uLightDir);
    vec3 v = normalize(uEye - fs.worldPosition);
    vec3 h = normalize(l + v);
    float nDotL = max(dot(n, l), 0.0);
    float nDotV = max(dot(n, v), 0.0);
    float nDotH = max(dot(n, h), 0.0);
    float vDotH = max(dot(v, h), 0.0);
    float roughness = clamp(uScientificRoughness, 0.08, 1.0);
    vec3 f0 = vec3(0.04);
    vec3 F = F_Schlick(vDotH, f0);
    float D = D_GGX(nDotH, roughness);
    float G = G_Schlick(nDotV, roughness) * G_Schlick(nDotL, roughness);
    vec3 specular = (D * G * F) / max(4.0 * nDotV * nDotL, 1.0e-4);
    vec3 albedo = vec3(0.48);
    vec3 diffuse = (vec3(1.0) - F) * albedo / PI;
    vec3 direct = (diffuse + specular) * vec3(2.6) * nDotL;
    return direct + albedo * vec3(0.08);
}

void main() {
    if (fs.validity < 0.5) discard;

    // Use the actually rasterized triangle plane as the scientific geometric normal.
    // This avoids giving coarse geometry the reference surface normal by accident.
    vec3 dx = dFdx(fs.worldPosition);
    vec3 dy = dFdy(fs.worldPosition);
    vec3 geometricNormal = normalize(cross(dx, dy));
    if (dot(geometricNormal, fs.normal) < 0.0) geometricNormal = -geometricNormal;
    vec3 smoothSurfaceNormal = normalize(fs.normal);
    if (dot(smoothSurfaceNormal, geometricNormal) < 0.0) smoothSurfaceNormal = -smoothSurfaceNormal;
    // Keep the shading normal on the same hemisphere as the actual triangle plane. This preserves
    // smooth lighting without allowing interpolation across very steep local folds to flip lighting.
    if (dot(smoothSurfaceNormal, geometricNormal) < 0.18) {
        smoothSurfaceNormal = normalize(mix(geometricNormal, smoothSurfaceNormal, 0.72));
    }

    bool useSmoothNormals = uShadingNormalMode == 2 || (uShadingNormalMode == 0 && uRenderMode != 0);
    vec3 shadingSurfaceNormal = useSmoothNormals ? smoothSurfaceNormal : geometricNormal;

    if (uRenderMode == 0) {
        outColor = vec4(scientificShading(shadingSurfaceNormal), 1.0);
        return;
    }

    if (uRenderMode == 2) {
        // Diagnostic heatmaps encode values directly. Do not modulate them by lighting:
        // otherwise one legend color would correspond to several displayed RGB values.
        outColor = vec4(lodColor(fs.lodLevel), 1.0);
        return;
    }

    if (uRenderMode == 3) {
        outColor = vec4(errorColor(fs.errorRatio), 1.0);
        return;
    }

    if (uRenderMode == 4) {
        // Visualize local normal change without taking derivatives of a derivative-derived normal.
        // fs.normal is the interpolated surface normal supplied by the mesh path.
        vec3 smoothNormal = normalize(fs.normal);
        float normalChange = length(dFdx(smoothNormal)) + length(dFdy(smoothNormal));
        float slope = 1.0 - clamp(smoothNormal.y, 0.0, 1.0);
        float variation = clamp(normalChange * 5.0 + slope * 0.35, 0.0, 1.0);
        vec3 low = vec3(0.04, 0.16, 0.72);
        vec3 medium = vec3(0.08, 0.78, 0.28);
        vec3 high = vec3(1.00, 0.68, 0.04);
        vec3 extreme = vec3(0.92, 0.08, 0.14);
        vec3 c = variation < 0.33
            ? mix(low, medium, variation / 0.33)
            : variation < 0.72
                ? mix(medium, high, (variation - 0.33) / 0.39)
                : mix(high, extreme, (variation - 0.72) / 0.28);
        outColor = vec4(c, 1.0);
        return;
    }


    if (uRenderMode == 5) {
        // Distance from the controller boundary errorRatio = 1.0. The narrow bright band marks
        // regions in which a small camera or parameter change can change the selected level.
        float signedMargin = fs.errorRatio - 1.0;
        float magnitude = clamp(abs(signedMargin) / 0.75, 0.0, 1.0);
        vec3 safe = vec3(0.05, 0.45, 0.90);
        vec3 threshold = vec3(0.98, 0.92, 0.30);
        vec3 exceeded = vec3(0.90, 0.10, 0.55);
        vec3 c = signedMargin <= 0.0
            ? mix(threshold, safe, magnitude)
            : mix(threshold, exceeded, magnitude);
        outColor = vec4(c, 1.0);
        return;
    }

    if (uRenderMode == 6) {
        float slopeDegrees = degrees(acos(clamp(abs(geometricNormal.y), 0.0, 1.0)));
        float t = clamp(slopeDegrees / 75.0, 0.0, 1.0);
        vec3 low = vec3(0.05, 0.28, 0.80);
        vec3 medium = vec3(0.10, 0.76, 0.40);
        vec3 high = vec3(0.98, 0.76, 0.08);
        vec3 extreme = vec3(0.90, 0.10, 0.08);
        vec3 c = t < 0.5
            ? mix(low, medium, t / 0.5)
            : t < 0.8
                ? mix(medium, high, (t - 0.5) / 0.3)
                : mix(high, extreme, (t - 0.8) / 0.2);
        outColor = vec4(c, 1.0);
        return;
    }

    float heightN = clamp((fs.worldPosition.y - uHeightMin) / max(uHeightMax - uHeightMin, 1.0e-4), 0.0, 1.0);
    vec3 baseSurfaceNormal = shadingSurfaceNormal;
    vec4 mw;
    baseMaterialWeights(baseSurfaceNormal, heightN, mw);
    if (uScene == 8) {
        mw.x *= 1.22;
        mw.w *= 1.15;
    } else if (uScene == 9) {
        mw.x *= 0.30;
        mw.y *= 1.38;
        mw.z *= 1.28;
        mw.w *= 1.20;
    } else if (uScene == 7) {
        mw.x *= 1.15;
        mw.z *= 1.20;
    }
    mw /= max(dot(mw, vec4(1.0)), 1.0e-5);

    vec3 grassC = triColor(uGrassDiffuse, fs.worldPosition, baseSurfaceNormal, 2.0);
    vec3 dirtC = triColor(uDirtDiffuse, fs.worldPosition, baseSurfaceNormal, 2.1);
    vec3 rockC = triColor(uRockDiffuse, fs.worldPosition, baseSurfaceNormal, 1.5);
    vec3 sandC = triColor(uSandDiffuse, fs.worldPosition, baseSurfaceNormal, 2.0);

    // The supplied gray-rock asset benefits from a darker treatment on steep cliffs.
    float slope = 1.0 - clamp(baseSurfaceNormal.y, 0.0, 1.0);
    rockC *= mix(vec3(0.82, 0.84, 0.83), vec3(0.43, 0.47, 0.50), smoothstep(0.30, 0.78, slope));
    vec3 albedo = grassC * mw.x + dirtC * mw.y + rockC * mw.z + sandC * mw.w;

    float grassR = triScalar(uGrassRoughness, fs.worldPosition, baseSurfaceNormal, 2.0);
    float dirtR = triScalar(uDirtRoughness, fs.worldPosition, baseSurfaceNormal, 2.1);
    float rockR = triScalar(uRockRoughness, fs.worldPosition, baseSurfaceNormal, 1.5);
    float sandR = triScalar(uSandRoughness, fs.worldPosition, baseSurfaceNormal, 2.0);
    float roughness = clamp(grassR * mw.x + dirtR * mw.y + rockR * mw.z + sandR * mw.w, 0.18, 1.0);

    float macroDetail = terrainFbm(fs.worldPosition.xz * 0.34 + vec2(3.7, -2.1));
    float fineDetail = terrainNoise2D(fs.worldPosition.xz * 4.6);
    albedo *= mix(0.82, 1.13, macroDetail);
    albedo *= mix(0.94, 1.06, fineDetail);
    roughness = clamp(roughness + (macroDetail - 0.5) * 0.12, 0.18, 1.0);

    // Material height is presentation-only relief information. It modulates shading instead of
    // moving vertices, keeping adaptive LOD seams watertight.
    float grassH = triScalar(uGrassHeight, fs.worldPosition, baseSurfaceNormal, 2.0);
    float dirtH = triScalar(uDirtHeight, fs.worldPosition, baseSurfaceNormal, 2.1);
    float rockH = triScalar(uRockHeight, fs.worldPosition, baseSurfaceNormal, 1.5);
    float sandH = triScalar(uSandHeight, fs.worldPosition, baseSurfaceNormal, 2.0);
    float relief = grassH * mw.x + dirtH * mw.y + rockH * mw.z + sandH * mw.w;
    albedo *= mix(0.90, 1.07, relief);
    roughness = clamp(roughness + (0.5 - relief) * 0.10, 0.18, 1.0);

    vec3 grassN = triNormal(uGrassNormal, fs.worldPosition, baseSurfaceNormal, 2.0);
    vec3 dirtN = triNormal(uDirtNormal, fs.worldPosition, baseSurfaceNormal, 2.1);
    vec3 rockN = triNormal(uRockNormal, fs.worldPosition, baseSurfaceNormal, 1.5);
    vec3 sandN = triNormal(uSandNormal, fs.worldPosition, baseSurfaceNormal, 2.0);
    vec3 materialNormal = normalize(grassN * mw.x + dirtN * mw.y + rockN * mw.z + sandN * mw.w);
    if (dot(materialNormal, baseSurfaceNormal) < 0.0) materialNormal = -materialNormal;
    materialNormal = normalize(mix(baseSurfaceNormal, materialNormal, 0.42));
    if (dot(materialNormal, baseSurfaceNormal) < 0.35) {
        materialNormal = normalize(mix(baseSurfaceNormal, materialNormal, 0.28));
    }
    vec3 n = materialNormal;

    // Moss/grass clings to some lower and medium-angle rock faces, helping break up the
    // uniform material regions without introducing another external asset.
    float mossNoise = terrainFbm(fs.worldPosition.xz * 0.62 + vec2(-8.0, 5.0));
    float moss = smoothstep(0.45, 0.72, mossNoise) * smoothstep(0.22, 0.48, slope) *
        (1.0 - smoothstep(0.58, 0.78, slope)) * (1.0 - smoothstep(0.70, 0.90, heightN));
    albedo = mix(albedo, grassC * vec3(0.72, 0.92, 0.50), moss * 0.72);

    float snow = snowWeight(baseSurfaceNormal, heightN);
    vec3 snowColor = mix(vec3(0.74, 0.79, 0.83), vec3(0.95, 0.97, 0.99), terrainFbm(fs.worldPosition.xz * 1.4));
    albedo = mix(albedo, snowColor, snow);
    roughness = mix(roughness, 0.78, snow);
    n = normalize(mix(n, baseSurfaceNormal, snow * 0.72));

    // Cheap presentation-only cavity term. It is intentionally not used by scientific mode.
    float cavityNoise = terrainFbm(fs.worldPosition.xz * 1.75 + vec2(13.0, 9.0));
    float cavity = mix(0.70, 1.0, smoothstep(0.22, 0.82, cavityNoise));
    cavity = mix(1.0, cavity, 0.50 + 0.50 * smoothstep(0.25, 0.80, slope));

    vec3 color = shadePbr(albedo, n, roughness, cavity);
    float distanceFade = clamp(length(uEye - fs.worldPosition) / 58.0, 0.0, 1.0);
    vec3 haze = vec3(0.30, 0.36, 0.43);
    color = mix(color, haze, distanceFade * distanceFade * 0.22);
    float edgeDistance = max(abs(fs.worldPosition.x), abs(fs.worldPosition.z)) / max(0.5 * uWorldSize, 1.0e-4);
    float edgeAlpha = 1.0 - smoothstep(0.91, 0.995, edgeDistance);
    outColor = vec4(color, edgeAlpha);
}
