#pragma once
#include "Core/camera.h"
#include "RHI/VulkanDevice.h"
#include "RHI/vk_types.h"
#include <string>

namespace Aero {
	namespace Renderer {

		class SceneRenderer {
		public:
			void init(Aero::RHI::VulkanDevice* device, uint32_t windowWidth, uint32_t windowHeight);
			void cleanup();
			void recreate_render_targets(uint32_t width, uint32_t height);

			void upload_scene(const SceneData& scene);
			bool submit_scene(const SceneData& scene, std::string* statusMessage = nullptr);
			bool reload_scene(const SceneData& scene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);
			bool reload_shaders_and_scene(const SceneData& scene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);
			const SceneStats& get_scene_stats() const { return _sceneStats; }

			void draw(VkCommandBuffer cmd, VkImageView targetImageView, const Camera& camera, uint32_t screenWidth, uint32_t screenHeight, bool useGPUDriven, VkQueryPool timestampQueryPool = VK_NULL_HANDLE);

		private:
			void init_pipelines();
			void init_bindless_descriptor();
			void init_depth_image(uint32_t width, uint32_t height);
			void destroy_depth_image();
			void update_global_descriptor_set();

			void update_bindless_texture(const AllocatedImage& image, uint32_t textureID);

		private:
			Aero::RHI::VulkanDevice* _renderDevice{ nullptr };
			DeletionQueue _deletionQueue;

			// --- ������������״̬ ---
			VkPipelineLayout _trianglePipelineLayout;
			VkPipeline _trianglePipeline;
			VkPipelineLayout _cullingPipelineLayout;
			VkPipeline _cullingPipeline;

			VkDescriptorPool _globalDescriptorPool;
			VkDescriptorSetLayout _globalSetLayout;
			VkDescriptorSet _globalDescriptorSet;

			// --- ��ȾĿ�� ---
			AllocatedImage _depthImage;
			VkFormat _depthImageFormat{ VK_FORMAT_D32_SFLOAT };
			VkSampler _defaultSamplerLinear;

			// --- ���� GPU ���� (δ������ AssetManager) ---
			GPUMeshBuffers _mainMeshBuffers;
			AllocatedBuffer _materialBuffer;
			AllocatedBuffer _instanceBuffer;
			AllocatedBuffer _drawIndirectBuffer;
			std::vector<AllocatedImage> _sceneTextures;
			std::vector<SubMesh> _renderables;
			uint32_t _instanceCount{ 0 };
			SceneStats _sceneStats;
		};
	}
}
