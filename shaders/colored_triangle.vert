#version 460

const vec3 positions[3] = vec3[3](
    vec3( 0.0, -0.5, 0.0),
    vec3( 0.5,  0.5, 0.0),
    vec3(-0.5,  0.5, 0.0)
);

const vec3 colors[3] = vec3[3](
    vec3(1.0, 0.0, 0.0), // 红
    vec3(0.0, 1.0, 0.0), // 绿
    vec3(0.0, 0.0, 1.0)  // 蓝
);

layout (location = 0) out vec3 outColor;

void main() {
    // gl_VertexIndex 是 Vulkan 内置变量，调用 vkCmdDraw(3, 1, 0, 0) 时会自动从 0 变到 2
    gl_Position = vec4(positions[gl_VertexIndex], 1.0);
    outColor = colors[gl_VertexIndex];
}