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
			_sceneRenderer.init(device, windowWidth, windowHeight);
			_renderDevice = device;
		}

		void RenderContext::cleanup() {
			_sceneRenderer.cleanup();
		}

		bool RenderContext::submit_scene(const GpuScene& gpuScene, std::string* statusMessage) {
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
			vkDeviceWaitIdle(_renderDevice->get_device());
			cleanup();
			init(_renderDevice, windowWidth, windowHeight);
			return submit_scene(gpuScene, statusMessage);
		}

		bool RenderContext::reload_shaders_dry_run(std::string* statusMessage) {
			return recompile_shader_binaries(statusMessage);
		}

		bool RenderContext::reload_shaders_and_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage) {
			if (!recompile_shader_binaries(statusMessage)) {
				return false;
			}
			return reload_scene(gpuScene, windowWidth, windowHeight, statusMessage);
		}

		void RenderContext::draw(VkCommandBuffer cmd, VkImageView targetImageView, const Camera& camera, uint32_t screenWidth, uint32_t screenHeight, bool useGPUDriven, VkQueryPool timestampQueryPool) {
			_sceneRenderer.draw(cmd, targetImageView, camera, screenWidth, screenHeight, useGPUDriven, timestampQueryPool);
		}

		void RenderContext::recreate_render_targets(uint32_t width, uint32_t height) {
			_sceneRenderer.recreate_render_targets(width, height);
		}

		const SceneStats& RenderContext::get_scene_stats() const {
			return _sceneRenderer.get_scene_stats();
		}

	} // namespace Renderer
} // namespace Aero
