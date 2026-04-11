#pragma once
#include "vk_types.h"
#include "camera.h"
#include "RHI/VulkanDevice.h"

namespace Aero {
	namespace Renderer {
		class SceneRenderer {
		public:
			void init(Aero::RHI::VulkanDevice* device, uint32_t windowWidth, uint32_t windowHeight);
			void cleanup();

			void upload_scene(const SceneData& scene);

			void draw(VkCommandBuffer cmd, VkImageView targetImageView, const Camera& camera, uint32_t screenWidth, uint32_t screenHeight, bool useGPUDriven);

		private:
			void init_pipelines();
			void init_bindless_descriptor();
			void init_depth_image(uint32_t width, uint32_t height);
			void update_global_descriptor_set();

			// 内部使用的资源上传辅助函数 (暂留)
			GPUMeshBuffers upload_mesh_data(const SceneData& scene);
			AllocatedBuffer upload_ssbo_data(size_t bufferSize, const void* data);
			AllocatedImage upload_texture(void* pixels, int width, int height, VkFormat format);
			void update_bindless_texture(const AllocatedImage& image, uint32_t textureID);

		private:
			Aero::RHI::VulkanDevice* _renderDevice{ nullptr };
			DeletionQueue _deletionQueue;

			// --- 管线与描述符状态 ---
			VkPipelineLayout _trianglePipelineLayout;
			VkPipeline _trianglePipeline;
			VkPipelineLayout _cullingPipelineLayout;
			VkPipeline _cullingPipeline;

			VkDescriptorPool _globalDescriptorPool;
			VkDescriptorSetLayout _globalSetLayout;
			VkDescriptorSet _globalDescriptorSet;

			// --- 渲染目标 ---
			AllocatedImage _depthImage;
			VkFormat _depthImageFormat{ VK_FORMAT_D32_SFLOAT };
			VkSampler _defaultSamplerLinear;

			// --- 场景 GPU 数据 (未来归属 AssetManager) ---
			GPUMeshBuffers _mainMeshBuffers;
			AllocatedBuffer _materialBuffer;
			AllocatedBuffer _instanceBuffer;
			AllocatedBuffer _drawIndirectBuffer;
			std::vector<AllocatedImage> _sceneTextures;
			std::vector<SubMesh> _renderables;
			uint32_t _instanceCount{ 0 };
		};
	}
}