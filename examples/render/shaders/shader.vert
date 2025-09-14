#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 frag_uv;

layout(push_constant) uniform PushConstants {
    mat4 proj;
    mat4 view;
} pc;

void main() {
    mat4 mvp = pc.proj * pc.view;
    gl_Position = mvp * vec4(in_position, 0.0, 1.0);
    frag_uv     = uv;
}