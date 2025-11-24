#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push { vec4 color; }
pc;

layout(set = 2, binding = 0) uniform sampler2D uTex;  // optional if pipeline attaches texture

void main() {
    // If texture bound, sample; otherwise use push-constant color
    vec4 tex = texture(uTex, v_uv);
    if (tex.a > 0.0) {
        outColor = tex * v_color;
    } else {
        outColor = pc.color * v_color;
    }
}
