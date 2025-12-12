#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform texture2D fontTex;
layout(set = 1, binding = 1) uniform sampler fontTex_sampler;

void main() {
    vec4 tex = texture(sampler2D(fontTex, fontTex_sampler), uv);
    outColor = tex * color;
}