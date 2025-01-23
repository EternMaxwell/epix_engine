#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 color;
layout(location = 3) in mat4 model;
layout(location = 7) in int textureIndex;
layout(location = 8) in int samplerIndex;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) flat out int fragTextureIndex;
layout(location = 3) flat out int fragSamplerIndex;

layout(std140, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 projection;
};

void main() {
    gl_Position      = projection * view * model * vec4(position, 1.0);
    fragTexCoord     = texCoord;
    fragColor        = color;
    fragTextureIndex = textureIndex;
    fragSamplerIndex = samplerIndex;
}
