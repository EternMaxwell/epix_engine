#version 450

#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 inTexcoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D tex[65536];
layout(set = 0, binding = 1) uniform sampler smp[65536];

layout(push_constant) uniform PushConstant {
    int imageIndex;
    int samplerIndex;
} pc;

void main() {
    outColor = inColor * texture(sampler2D(tex[pc.imageIndex], smp[pc.samplerIndex]), inTexcoord);
}