#version 460 core

in vec2 vUv;
layout(location = 0) out vec4 outColor;
uniform sampler2D uColor;

void main() {
    vec3 hdr = texture(uColor, vUv).rgb;
    vec3 mapped = hdr / (vec3(1.0) + hdr);
    outColor = vec4(pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
