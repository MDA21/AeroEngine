#version 460

layout(location = 0) in vec3 vPosition;
layout(location = 1) in float vUvX;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in float vUvY;
layout(location = 4) in vec4 vTangent;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormal;

// 匹配我们在 C++ 里的 Push Constants
layout(push_constant) uniform constants {
    mat4 render_matrix;
    uint material_id;
} PushConstants;

void main() {
    gl_Position = PushConstants.render_matrix * vec4(vPosition, 1.0f);
    outUV = vec2(vUvX, vUvY);
    outNormal = vNormal; // 目前只传基础法线，后面我们再做切线空间法线贴图
}