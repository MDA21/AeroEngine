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
};

struct MaterialParams
{
	glm::vec4 baseColorFactor;
	glm::vec4 pbrFactors;

	int32_t albedoTexIdx;
	int32_t normalTexIdx;
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

struct SceneData
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<SubMesh> submeshes;
	std::vector<MaterialParams> materials;
};