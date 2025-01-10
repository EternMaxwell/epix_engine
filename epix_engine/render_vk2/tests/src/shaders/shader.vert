#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in mat4 any;
layout(location = 5) in int index;

layout(location = 0) out vec3 outPosition;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} pushConstant[256];

layout(binding = 1) buffer TransformBuffer {
    mat4 model;
} transformBuffer[256];

layout(set = 0, binding = 2) uniform sampler2D textureSamplers[256];
layout(set = 0, binding = 2) uniform sampler3D textureSamplers3D[256];
layout(binding = 3) uniform sampler textureSampler;

void main() {
    mat4 model = transformBuffer[index].model;
    vec4 color1 = texture(textureSamplers[index], vec2(0.5, 0.5));
    vec4 color2 = texture(textureSamplers3D[index], vec3(0.5, 0.5, 0.5));
    outPosition = vec3(
        pushConstant[index].proj * pushConstant[index].view * model *
        vec4(inPosition, 1.0)
    );
    gl_Position = pushConstant[index].proj * pushConstant[index].view * model *
                  vec4(inPosition, 1.0);
}