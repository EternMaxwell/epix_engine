#version 450

layout(location = 0) in vec3 v_normal;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(v_normal);
    // simple lighting: use normal to produce color for debug/flat shading
    float intensity = max(dot(normalize(vec3(0.0, 0.0, 1.0)), n) * 0.5 + 0.5, 0.0);
    outColor        = vec4(vec3(intensity), 1.0);
}
