#version 450

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;

layout(set = 1, binding = 0) uniform texture2D texture_image;
layout(set = 1, binding = 1) uniform sampler texture_sampler;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(sampler2D(texture_image, texture_sampler), frag_uv) * frag_color;
}