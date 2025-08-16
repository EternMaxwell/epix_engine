#version 450

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler texture_sampler;
layout(set = 0, binding = 1) uniform texture2D sampled_texture;

void main() {
    out_color = texture(sampler2D(sampled_texture, texture_sampler), uv);
}
