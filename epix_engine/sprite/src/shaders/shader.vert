#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;

layout(set = 0, binding = 0) uniform Camera {
    mat4 projection;
    mat4 view;
} camera;
struct Instance {
    mat4 model;
    vec4 uv_offset_scale;
    vec4 color;
    vec4 pos_offset_scale;
};
layout(set = 1, binding = 0) buffer InstanceData {
    Instance instances[];
};

void main() {
    mat4 mvp = camera.projection * camera.view * instances[gl_InstanceIndex].model;
    gl_Position = mvp * vec4((in_position + instances[gl_InstanceIndex].pos_offset_scale.xy) * instances[gl_InstanceIndex].pos_offset_scale.zw, 0.0, 1.0);
    frag_uv = in_uv * instances[gl_InstanceIndex].uv_offset_scale.zw + instances[gl_InstanceIndex].uv_offset_scale.xy;
    frag_color = instances[gl_InstanceIndex].color;
}