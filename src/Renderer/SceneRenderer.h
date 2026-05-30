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

			// 绑定 GpuScene：生成 InstanceData/Indirect 并更新 Bindless 描述符
			void bind_scene(const GpuScene& gpuScene);

			bool submit_scene(const GpuScene& gpuScene, std::string* statusMessage = nullptr);
			bool reload_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);
			bool reload_shaders_and_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);

			// 仅重编译着色器，不修改场景状态（用于重载前的预检）
			bool reload_shaders_and_scene_dry_run(std::string* statusMessage = nullptr);

			const SceneStats& get_scene_stats() const;

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

			// --- 管线状态 ---
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

			// --- 渲染器私有的 GPU 资源（Instance/Indirect 由渲染器生成，不归 AssetManager） ---
			AllocatedBuffer _instanceBuffer;
			AllocatedBuffer _drawIndirectBuffer;
			uint32_t _instanceCount{ 0 };

			// 当前绑定的场景（非持有指针，由 AssetManager 持有并负责销毁）
			const GpuScene* _currentScene{ nullptr };
		};
	}
}
