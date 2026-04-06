#version 460
#extension GL_EXT_nonuniform_qualifier : require

// 1. 接收来自 Vertex Shader 的输入
layout (location = 0) in vec2 inUV;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in flat uint inMaterialID; // 必须用 flat 关闭插值

// 2. 输出到 Framebuffer
layout (location = 0) out vec4 outFragColor;

// 3. Bindless 描述符绑定
// Binding 0: 材质大数组 (和 C++ vk_types.h 中的 MaterialParams 严格对齐)
struct MaterialParams {
    vec4 baseColorFactor;
    vec4 pbrFactors;
    int albedoTexIdx;
    int normalTexIdx;
    int pbrTexIdx;
    int emissiveTexIdx;
};

layout(std430, set = 0, binding = 0) readonly buffer MaterialBuffer {
    MaterialParams materials[];
};

// Binding 1: 场景所有贴图数组
layout(set = 0, binding = 1) uniform sampler2D globalTextures[];

void main() {
    // 根据顶点着色器传来的 ID 获取材质数据
    MaterialParams mat = materials[inMaterialID];
    
    vec4 baseColor = mat.baseColorFactor;
    
    // 如果有漫反射贴图，则采样
    if (mat.albedoTexIdx >= 0) {
        // 工业标准：在 Bindless 架构中根据动态变量采样贴图数组时，必须加 nonuniformEXT 修饰符
        baseColor *= texture(globalTextures[nonuniformEXT(mat.albedoTexIdx)], inUV);
    }
    
    // 基础的平行光漫反射 (Lambert)，为了让你看清模型立体感
    vec3 N = normalize(inNormal);
    vec3 L = normalize(vec3(0.5, 1.0, 0.5)); // 写死一个光源方向
    float NdotL = max(dot(N, L), 0.05);      // 0.05 作为基础环境光
    
    outFragColor = vec4(baseColor.rgb * NdotL, baseColor.a);
}