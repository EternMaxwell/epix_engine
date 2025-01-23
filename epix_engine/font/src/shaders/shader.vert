#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 uv1;
layout(location = 3) in vec2 uv2;
layout(location = 4) in vec2 size;
layout(location = 5) in int layer;
layout(location = 6) in int texture_index;
layout(location = 7) in int sampler_index;

layout(location = 0) out vec2 pos;
layout(location = 1) out vec2 tex_coord_lb;
layout(location = 2) out vec2 tex_coord_rt;
layout(location = 3) out vec2 size_o;
layout(location = 4) out int layer_id;
layout(location = 5) out int texture_id;
layout(location = 6) out vec4 col;
layout(location = 7) out int sampler_id;

void main() {
    pos          = position;
    tex_coord_lb = uv1;
    tex_coord_rt = uv2;
    size_o       = size;
    layer_id     = layer;
    texture_id   = texture_index;
    col          = color;
    sampler_id   = sampler_index;
}