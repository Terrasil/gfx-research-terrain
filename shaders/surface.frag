#version 460 core

in VS_OUT {
    vec3 worldPosition;
    vec3 normal;
    vec2 uv;
} fs;

layout(location = 0) out vec4 outColor;

uniform vec3 uEye;
uniform vec3 uLightDir;
uniform int uLod;
uniform bool uVisualizeLod;

vec3 lodColor(int lod) {
    const vec3 colors[6] = vec3[6](
        vec3(0.80, 0.20, 0.18),
        vec3(0.95, 0.52, 0.15),
        vec3(0.85, 0.78, 0.18),
        vec3(0.25, 0.72, 0.28),
        vec3(0.18, 0.48, 0.90),
        vec3(0.62, 0.30, 0.86)
    );
    return colors[clamp(lod, 0, 5)];
}

void main() {
    vec3 n = normalize(fs.normal);
    vec3 l = normalize(-uLightDir);
    vec3 v = normalize(uEye - fs.worldPosition);
    vec3 h = normalize(l + v);
    float ndotl = max(dot(n, l), 0.0);
    float spec = pow(max(dot(n, h), 0.0), 48.0);
    float slope = 1.0 - clamp(n.y, 0.0, 1.0);
    vec3 base = mix(vec3(0.16, 0.33, 0.13), vec3(0.33, 0.31, 0.29), slope);
    vec3 lit = base * (0.18 + 0.82 * ndotl) + vec3(0.22) * spec;
    if (uVisualizeLod) lit = mix(lit, lodColor(uLod), 0.72);
    outColor = vec4(lit, 1.0);
}
