#pragma once
#include <volk.h>
#include <vk_mem_alloc.h>
#include <iostream>
#include <deque>
#include <functional>
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>

constexpr unsigned int FRAME_OVERLAP = 2;

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;
	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}
	void flush() {
		for (auto it = deletors.rbegin(); it != deletors.rend(); ++it) {
			(*it)();
		}
		deletors.clear();
	}
};

#define VK_CHECK(x)                                                 \
    do {                                                            \
        VkResult err = x;                                           \
        if (err) {                                                  \
            std::cerr << "Detected Vulkan error: " << err << "\n";  \
            abort();                                                \
        }                                                           \
    } while (0)

struct Vertex
{
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 tangent;

	static VkVertexInputBindingDescription getBindingDescription() {
		VkVertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(Vertex);
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return bindingDescription;
	}

	static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions(5);

		// Location 0: Position (vec3)
		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = offsetof(Vertex, position);

		// Location 1: UV_X (float)
		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(Vertex, uv_x);

		// Location 2: Normal (vec3)
		attributeDescriptions[2].binding = 0;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[2].offset = offsetof(Vertex, normal);

		// Location 3: UV_Y (float)
		attributeDescriptions[3].binding = 0;
		attributeDescriptions[3].location = 3;
		attributeDescriptions[3].format = VK_FORMAT_R32_SFLOAT;
		attributeDescriptions[3].offset = offsetof(Vertex, uv_y);

		// Location 4: Tangent (vec4)
		attributeDescriptions[4].binding = 0;
		attributeDescriptions[4].location = 4;
		attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[4].offset = offsetof(Vertex, tangent);

		return attributeDescriptions;
	}
};

struct MaterialParams
{
	glm::vec4 baseColorFactor;
	glm::vec4 pbrFactors;

	int32_t albedoTexIdx;
	int32_t normalTexIdx;
	int32_t pbrTexIdx;
	int32_t emissiveTexIdx;
};

struct SubMesh
{
	uint32_t firstIndex;
	uint32_t indexCount;
	uint32_t vertexOffset;
	uint32_t materialIndex;

	alignas(16) glm::vec3 aabbMin;
	alignas(16) glm::vec3 aabbMax;
};

struct LoadedImage {
	unsigned char* pixels{ nullptr };
	int width{ 0 };
	int height{ 0 };
	int channels{ 0 };
};

struct SceneData
{
	uint32_t meshCount{ 0 };
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<SubMesh> subMeshes;
	std::vector<MaterialParams> materials;
	std::vector<LoadedImage> images;
};

struct AllocatedBuffer
{
	VkBuffer buffer;
	VmaAllocation allocation;
};

struct AllocatedImage {
	VkImage image;
	VkImageView view;
	VmaAllocation allocation;
	VkExtent3D imageExtent;
	VkFormat imageFormat;
};

struct GPUMeshBuffers
{
	AllocatedBuffer vertexBuffer;
	AllocatedBuffer indexBuffer;
};

// ============================================================
// SceneStats: 场景统计信息，供调试面板展示
// 从 SceneRenderer.h 迁移到此处，成为项目共享类型
// ============================================================
struct SceneStats {
	uint32_t meshCount{ 0 };
	uint32_t submeshCount{ 0 };
	uint32_t materialCount{ 0 };
	uint32_t textureCount{ 0 };
	uint32_t vertexCount{ 0 };
	uint32_t indexCount{ 0 };
};

// ============================================================
// GpuScene: AssetManager 上传完成后返回的 GPU 资源句柄集
//
// 设计原则：
// - 只包含"场景数据"本身的 GPU 资源（顶点/索引/材质/纹理）
// - Instance/Indirect buffer 不属于这里 —— 它们是渲染器根据 subMeshes
//   动态生成的，是"渲染策略"而非"场景数据"
// - subMeshes 和 materials 是 CPU 端的元数据副本，渲染器生成
//   InstanceData 时需要读 AABB、材质索引等信息
//
// 生命周期：
// - 由 AssetManager 创建和销毁（通过 upload_scene / unload_scene）
// - SceneRenderer 通过 const* 借用，不持有所有权
// ============================================================
struct GpuScene {
	GPUMeshBuffers meshBuffers;                // 顶点 + 索引 buffer
	AllocatedBuffer materialBuffer;            // MaterialParams SSBO
	std::vector<AllocatedImage> textures;      // 纹理数组（image + view）
	std::vector<SubMesh> subMeshes;            // CPU 端元数据（AABB、索引范围等）
	std::vector<MaterialParams> materials;     // CPU 端材质参数
	SceneStats stats;                          // 统计信息

	// 至少顶点 buffer 创建成功即视为有效场景
	bool valid() const {
		return meshBuffers.vertexBuffer.buffer != VK_NULL_HANDLE;
	}
};

//for gpu driven instanse data
struct InstanceData
{
	glm::mat4 modelMatrix;
	glm::vec4 aabbMin_MatID;
	glm::vec4 aabbMax_Pad;
	uint32_t indexCount;
	uint32_t firstIndex;
	int32_t vertexOffset;
	uint32_t padding;
};
