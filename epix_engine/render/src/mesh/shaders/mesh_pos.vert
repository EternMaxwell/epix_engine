#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform View {
    mat4 projection;
    mat4 view;
}
viewUBO;

layout(set = 1, binding = 0, std430) buffer Mesh { mat4 model; }
meshSSBO;

void main() { gl_Position = viewUBO.projection * viewUBO.view * meshSSBO.model * vec4(inPosition, 1.0); }
