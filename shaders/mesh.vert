#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in float vUvX;
layout (location = 2) in vec3 vNormal;
layout (location = 3) in float vUvY;
layout (location = 4) in vec4 vTangent;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out flat uint outMaterialID;

// 定义和 C++ 完全一致的内存布局
struct InstanceData {
    mat4 modelMatrix;
    vec4 aabbMin_MatID;
    vec4 aabbMax;
    uint indexCount;
    uint firstIndex;
    int  vertexOffset;
    uint padding;
};

// 绑定我们刚创建的 SSBO (Binding 2)
layout(std430, set = 0, binding = 2) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

// PushConstant 现在只负责传一个全局的 ViewProj 矩阵
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pushData;

void main() {
    // 核心魔法：根据当前绘制的实例 ID，去数组里抓数据
    InstanceData inst = instances[gl_InstanceIndex];
    mat4 model = inst.modelMatrix;
    
    gl_Position = pushData.viewProj * model * vec4(vPosition, 1.0);
    
    outUV = vec2(vUvX, vUvY);
    outNormal = mat3(model) * vNormal; 
    
    // floatBitsToUint 转回无符号整型
    outMaterialID = floatBitsToUint(inst.aabbMin_MatID.w);
}