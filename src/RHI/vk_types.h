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