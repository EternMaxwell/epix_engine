#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in uint model_id;

layout(location = 0) out vec4 color;

layout(std140, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 projection;
};

layout(push_constant) uniform Model { mat4 model; };

void main() {
    gl_Position = projection * view * model * vec4(in_position, 1.0);
    color       = in_color;
}