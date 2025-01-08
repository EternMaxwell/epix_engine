#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in int index;

layout(location = 0) out vec3 outPosition;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} pushConstant[256];

layout(binding = 1) buffer TransformBuffer {
    mat4 model;
} transformBuffer[256];

layout(binding = 2) uniform sampler2D textureSampler[256];

void main() {
    mat4 model = transformBuffer[index].model;
    outPosition = vec3(
        pushConstant[index].proj * pushConstant[index].view * model *
        vec4(inPosition, 1.0)
    );
    gl_Position = pushConstant[index].proj * pushConstant[index].view * model *
                  vec4(inPosition, 1.0);
}