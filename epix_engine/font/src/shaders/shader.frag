#version 450

#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 tex_coords;
layout(location = 1) in vec4 color;
layout(location = 2) flat in int tex_id;
layout(location = 3) flat in int sampler_id;

layout(location = 0) out vec4 frag_color;

layout(set = 1, binding = 0) uniform utexture2DArray tex[];
layout(set = 1, binding = 1) uniform sampler fontsampler[];

void main() {
    float v =
        texture(
            usampler2DArray(tex[tex_id], fontsampler[sampler_id])
            , tex_coords
        ).r / 255.0;
    frag_color = v * color;
}