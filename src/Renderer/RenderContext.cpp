#include "RenderContext.h"
#include "RHI/VulkanDevice.h"
#include <iostream>
#include <array>
#include <filesystem>
#include <cstdlib>
#include <Windows.h>

namespace Aero {
	namespace Renderer {

		// ============================================================
		// 文件级工具函数（从 SceneRenderer.cpp 搬移过来）
		// ============================================================

		static std::filesystem::path get_executable_directory() {
			char modulePath[MAX_PATH]{};
			DWORD pathLength = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
			return std::filesystem::path(std::string(modulePath, pathLength)).parent_path();
		}

		static bool recompile_shader_binaries(std::string* statusMessage) {
			// TODO #2: 遍历 {"mesh.vert", "mesh.frag", "culling.comp"}，
			// 对每个 shader 调用 glslc 编译为 .spv
			// 1. 通过 get_executable_directory() 定位 shader 源文件目录和输出目录
			// 2. 从 VULKAN_SDK 环境变量查找 glslc.exe
			// 3. 用 std::system 执行编译命令
			// 4. 任一失败则返回 false
			const std::filesystem::path exeDir = get_executable_directory();
			const std::filesystem::path sourceDir = exeDir.parent_path().parent_path() / "shaders";
			const std::filesystem::path outputDir = exeDir / "shaders";
			std::filesystem::create_directories(outputDir);

			std::string glslcCommand = "glslc";
			if (const char* vulkanSdk = std::getenv("VULKAN_SDK")) {
				std::filesystem::path glslcPath = std::filesystem::path(vulkanSdk) / "Bin" / "glslc.exe";
				if (std::filesystem::exists(glslcPath)) {
					glslcCommand = "\"" + glslcPath.string() + "\"";
				}
			}

			const std::array<std::string, 3> shaderNames = {"mesh.vert", "mesh.frag", "culling.comp"};
			for (const std::string& shaderName : shaderNames) {
				const std::filesystem::path sourcePath = sourceDir / shaderName;
				const std::filesystem::path outputPath = outputDir / (shaderName + ".spv");
				const std::string command = glslcCommand + " \"" + sourcePath.string() + "\" -o \"" + outputPath.string() + "\"";

				std::cout << "[Reload] source: " << sourcePath << std::endl;
				std::cout << "[Reload] output: " << outputPath << std::endl;
				std::cout << "[Reload] command: " << command << std::endl;

				if (std::system(command.c_str()) != 0) {
					if (statusMessage) {
						*statusMessage = "Shader compile failed: " + shaderName;
					}
					return false;
				}
			}

			return true;
		}

		// ============================================================
		// RenderContext 实现
		// ============================================================

		void RenderContext::init(Aero::RHI::VulkanDevice* device, uint32_t windowWidth, uint32_t windowHeight) {
			// TODO #3: 委托给 _sceneRenderer.init(device, windowWidth, windowHeight)
			_sceneRenderer.init(device, windowWidth, windowHeight);
			_renderDevice = device;
		}

		void RenderContext::cleanup() {
			// TODO #4: 委托给 _sceneRenderer.cleanup()
			_sceneRenderer.cleanup();
		}

		bool RenderContext::submit_scene(const GpuScene& gpuScene, std::string* statusMessage) {
			// TODO #5: 协调流程 —
			// 1. 调用 _sceneRenderer.bind_scene(gpuScene)
			// 2. 调用 _sceneRenderer.is_scene_bound() 验证 buffer 创建成功
			// 3. 设置 statusMessage（成功/失败信息）
			// 4. 返回 true/false
			_sceneRenderer.bind_scene(gpuScene);
			if (_sceneRenderer.is_scene_bound()) {
				if (statusMessage)
					*statusMessage = "Scene submitted successfully.";
				return true;
			} else {
				if (statusMessage)
					*statusMessage = "Failed to create GPU buffers for the scene.";
				return false;
			}
		}

		bool RenderContext::reload_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage) {
			// TODO #6: 完整重载 —
			// 1. vkDeviceWaitIdle
			// 2. cleanup()
			// 3. init(device, windowWidth, windowHeight)
			// 4. submit_scene(gpuScene, statusMessage)
			// 注意：init 需要 VulkanDevice*，但 RenderContext 不持有它
			// → 需要在 RenderContext 中加一个 _renderDevice 成员，在 init() 时保存
			vkDeviceWaitIdle(_renderDevice->get_device());
			cleanup();
			init(_renderDevice, windowWidth, windowHeight);
			return submit_scene(gpuScene, statusMessage);
		}

		bool RenderContext::reload_shaders_dry_run(std::string* statusMessage) {
			// TODO #7: 仅重编译 shader，不修改场景状态
			// → 调用 recompile_shader_binaries(statusMessage)
			return recompile_shader_binaries(statusMessage);
		}

		bool RenderContext::reload_shaders_and_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage) {
			// TODO #8: shader 重编译 + 场景重载 —
			// 1. 先调用 recompile_shader_binaries，失败则立即返回 false
			// 2. 再调用 reload_scene(gpuScene, windowWidth, windowHeight, statusMessage)
			if (!recompile_shader_binaries(statusMessage)) {
				return false;
			}
			return reload_scene(gpuScene, windowWidth, windowHeight, statusMessage);
		}

		void RenderContext::draw(VkCommandBuffer cmd, VkImageView targetImageView, const Camera& camera, uint32_t screenWidth, uint32_t screenHeight, bool useGPUDriven, VkQueryPool timestampQueryPool) {
			// TODO #9: 委托给 _sceneRenderer.draw(...)
			_sceneRenderer.draw(cmd, targetImageView, camera, screenWidth, screenHeight, useGPUDriven, timestampQueryPool);
		}

		void RenderContext::recreate_render_targets(uint32_t width, uint32_t height) {
			// TODO #10: 委托给 _sceneRenderer.recreate_render_targets(width, height)
			_sceneRenderer.recreate_render_targets(width, height);
		}

		const SceneStats& RenderContext::get_scene_stats() const {
			// TODO #11: 委托给 _sceneRenderer.get_scene_stats()
			return _sceneRenderer.get_scene_stats();
		}

	} // namespace Renderer
} // namespace Aero
