#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

struct MaterialParams {
    vec4 baseColorFactor;
    vec4 pbrFactors; // x: roughness, y: metallic
    int albedoTexIdx;
    int normalTexIdx;
    int pbrTexIdx;
    int emissiveTexIdx;
};

// 我们的全局大水缸！
layout(set = 0, binding = 0) readonly buffer MaterialBuffer {
    MaterialParams materials[];
} materialData;

layout(set = 0, binding = 1) uniform sampler2D globalTextures[];

layout(push_constant) uniform constants {
    mat4 render_matrix;
    uint material_id;
} PushConstants;

void main() {
    // 1. O(1) 提取当前材质
    MaterialParams mat = materialData.materials[PushConstants.material_id];
    
    // 2. 获取基础色因子
    vec4 color = mat.baseColorFactor;
    
    // 3. Bindless 动态采样 (注意 nonuniformEXT 防止 GPU 线程分歧崩溃)
    if (mat.albedoTexIdx >= 0) {
        color *= texture(globalTextures[nonuniformEXT(mat.albedoTexIdx)], inUV);
    }
    
    // 简单的环境光打底，验证法线方向
    vec3 ambient = vec3(0.2) * color.rgb;
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(normalize(inNormal), lightDir), 0.0);
    vec3 diffuse = diff * color.rgb;

    outColor = vec4(ambient + diffuse, color.a);
}