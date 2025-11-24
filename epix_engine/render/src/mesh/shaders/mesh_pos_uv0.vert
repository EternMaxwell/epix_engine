#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 inUV0;

layout(location = 0) out vec2 v_uv;

layout(set = 0, binding = 0) uniform View {
    mat4 projection;
    mat4 view;
}
viewUBO;

layout(set = 1, binding = 0) uniform Mesh { mat4 model; }
meshUBO;

void main() {
    gl_Position = viewUBO.projection * viewUBO.view * meshUBO.model * vec4(inPosition, 1.0);
    v_uv        = inUV0;
}
