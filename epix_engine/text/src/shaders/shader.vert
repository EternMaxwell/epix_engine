#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV0;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

layout(set = 0, binding = 0) uniform View {
    mat4 projection;
    mat4 view;
}
viewUBO;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
}
pushConstants;

void main() {
    gl_Position = viewUBO.projection * viewUBO.view * pushConstants.model * vec4(inPosition, 1.0);
    v_uv        = inUV0;
    v_color     = pushConstants.color;
}
