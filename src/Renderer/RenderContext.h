#pragma once
#include "SceneRenderer.h"
#include <string>

namespace Aero {
	namespace Renderer {

		class RenderContext {
		public:
			void init(Aero::RHI::VulkanDevice* device, uint32_t windowWidth, uint32_t windowHeight);
			void cleanup();

			bool submit_scene(const GpuScene& gpuScene, std::string* statusMessage = nullptr);
			bool reload_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);
			bool reload_shaders_dry_run(std::string* statusMessage = nullptr);
			bool reload_shaders_and_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);

			void draw(VkCommandBuffer cmd, VkImageView targetImageView, const Camera& camera, uint32_t screenWidth, uint32_t screenHeight, bool useGPUDriven, VkQueryPool timestampQueryPool = VK_NULL_HANDLE);
			void recreate_render_targets(uint32_t width, uint32_t height);
			const SceneStats& get_scene_stats() const;

		private:
			SceneRenderer _sceneRenderer;
			Aero::RHI::VulkanDevice* _renderDevice{nullptr};
		};

	} // namespace Renderer
} // namespace Aero
