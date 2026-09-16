#version 460 core

in vec2 vUv;
layout(location = 0) out vec4 outColor;
uniform sampler2D uColor;
uniform int uRenderMode;

vec3 skyColor(vec2 uv) {
    float horizon = smoothstep(0.08, 0.72, uv.y);
    vec3 low = vec3(0.47, 0.54, 0.62);
    vec3 high = vec3(0.10, 0.18, 0.30);
    vec3 sky = mix(low, high, horizon);

    vec2 sunPosition = vec2(0.76, 0.76);
    float sunDistance = length((uv - sunPosition) * vec2(1.0, 1.65));
    float glow = exp(-sunDistance * 8.5);
    sky += vec3(0.72, 0.43, 0.20) * glow * 0.28;
    return sky;
}

void main() {
    vec4 sampleColor = texture(uColor, vUv);
    vec3 hdr = sampleColor.rgb;

    if (uRenderMode == 1) {
        hdr = mix(skyColor(vUv), hdr, clamp(sampleColor.a, 0.0, 1.0));
    } else if (sampleColor.a < 0.5) {
        hdr = vec3(0.025, 0.030, 0.040);
    }

    if (uRenderMode >= 2) {
        outColor = vec4(pow(max(hdr, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec3 mapped = hdr / (vec3(1.0) + hdr);
    outColor = vec4(pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
