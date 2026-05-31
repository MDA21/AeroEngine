#pragma once
#include "SceneRenderer.h"
#include <string>

namespace Aero {
	namespace Renderer {

		// 渲染能力的统一门面，负责场景绑定/热重载等协调逻辑
		// SceneRenderer 退居为纯渲染执行器，不持有场景引用、不参与协调
		class RenderContext {
		public:
			void init(Aero::RHI::VulkanDevice* device, uint32_t windowWidth, uint32_t windowHeight);
			void cleanup();

			// ---- 场景生命周期（协调 AssetManager ↔ SceneRenderer） ----

			// 绑定 GpuScene 到渲染器：生成 Instance/Indirect buffer + 更新描述符
			bool submit_scene(const GpuScene& gpuScene, std::string* statusMessage = nullptr);

			// 完整重载场景（先 cleanup → init → submit）
			bool reload_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);

			// 仅重编译 shader 二进制（用于重载前预检）
			bool reload_shaders_dry_run(std::string* statusMessage = nullptr);

			// 重编译 shader + 重载场景
			bool reload_shaders_and_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);

			// ---- 渲染（委托给 SceneRenderer） ----

			void draw(VkCommandBuffer cmd, VkImageView targetImageView, const Camera& camera, uint32_t screenWidth, uint32_t screenHeight, bool useGPUDriven, VkQueryPool timestampQueryPool = VK_NULL_HANDLE);

			void recreate_render_targets(uint32_t width, uint32_t height);

			const SceneStats& get_scene_stats() const;

		private:
			SceneRenderer _sceneRenderer;
			Aero::RHI::VulkanDevice* _renderDevice{nullptr};
		};

	} // namespace Renderer
} // namespace Aero
