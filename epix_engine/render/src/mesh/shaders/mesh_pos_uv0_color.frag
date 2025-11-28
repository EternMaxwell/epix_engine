#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform texture2D uTex;
layout(set = 2, binding = 1) uniform sampler uTex_sampler;

void main() {
    vec4 tex = texture(sampler2D(uTex, uTex_sampler), v_uv);
    // Multiply vertex color with texture (modulate)
    outColor = tex * v_color;
}
