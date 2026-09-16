#version 460 core

in vec2 vUv;
layout(location = 0) out vec4 outColor;

uniform sampler2D uReference;
uniform sampler2D uCandidate;
uniform float uErrorMax;

vec3 errorPalette(float t) {
    t = clamp(t, 0.0, 1.0);
    const vec3 c0 = vec3(0.004, 0.000, 0.015);
    const vec3 c1 = vec3(0.190, 0.055, 0.365);
    const vec3 c2 = vec3(0.550, 0.160, 0.505);
    const vec3 c3 = vec3(0.870, 0.290, 0.410);
    const vec3 c4 = vec3(0.995, 0.625, 0.425);
    const vec3 c5 = vec3(0.990, 0.990, 0.750);
    float x = t * 5.0;
    int segment = min(int(floor(x)), 4);
    float f = fract(x);
    if (segment == 0) return mix(c0, c1, f);
    if (segment == 1) return mix(c1, c2, f);
    if (segment == 2) return mix(c2, c3, f);
    if (segment == 3) return mix(c3, c4, f);
    return mix(c4, c5, f);
}

void main() {
    vec3 reference = texture(uReference, vUv).rgb;
    vec3 candidate = texture(uCandidate, vUv).rgb;
    float error = sqrt(dot(reference - candidate, reference - candidate) / 3.0);
    float normalized = error / max(uErrorMax, 1.0e-6);
    outColor = vec4(errorPalette(normalized), 1.0);
}
