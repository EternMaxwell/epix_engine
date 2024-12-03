#version 450

layout(points) in;

layout(triangle_strip, max_vertices = 6) out;

layout(std140, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 projection;
};

layout(std140, binding = 1) buffer ModelBuffer { mat4 models[]; };

layout(location = 0) in vec2 geom_pos[];
layout(location = 1) in vec4 geom_color[];
layout(location = 2) in int geom_model_id[];

layout(location = 0) out vec4 frag_color;

void main() {
    mat4 mvp = projection * view * models[geom_model_id[0]];

    gl_Position = mvp * vec4(geom_pos[0], 0, 1);
    frag_color  = geom_color[0];
    EmitVertex();
    gl_Position = mvp * vec4(geom_pos[0].x + 1, geom_pos[0].y, 0, 1);
    frag_color  = geom_color[0];
    EmitVertex();
    gl_Position = mvp * vec4(geom_pos[0].x + 1, geom_pos[0].y + 1, 0, 1);
    frag_color  = geom_color[0];
    EmitVertex();
    EndPrimitive();
    gl_Position = mvp * vec4(geom_pos[0].x, geom_pos[0].y, 0, 1);
    frag_color  = geom_color[0];
    EmitVertex();
    gl_Position = mvp * vec4(geom_pos[0].x + 1, geom_pos[0].y + 1, 0, 1);
    frag_color  = geom_color[0];
    EmitVertex();
    gl_Position = mvp * vec4(geom_pos[0].x, geom_pos[0].y + 1, 0, 1);
    frag_color  = geom_color[0];
    EmitVertex();
    EndPrimitive();
}