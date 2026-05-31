#include "aero_engine.h"
#include <iostream>
#include <optional>
#include <array>
#include <chrono>
#include <filesystem>
#include <Windows.h>
#include "RHI/vk_initializers.h"
#include "Core/KeyCodes.h"
#include "Resource/asset_manager.h"
#include "Resource/gltf_loader.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace {
	std::filesystem::path get_executable_directory() {
		char modulePath[MAX_PATH] = {};
		DWORD pathLength = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
		return std::filesystem::path(std::string(modulePath, pathLength)).parent_path();
	}
} // namespace

AeroEngine& AeroEngine::Get() {
	static AeroEngine engine;
	return engine;
}

void AeroEngine::init() {
	std::filesystem::current_path(get_executable_directory());

	_window = std::make_unique<Aero::Window>(Aero::Window::Specs{1280, 720, "AeroEngine v0.1"});

	_renderDevice = std::make_unique<Aero::RHI::VulkanDevice>();
	_renderDevice->init(_window.get(), _mainDeletionQueue);

	Aero::Resource::AssetManager::Get().init(_renderDevice.get());

	_mainDeletionQueue.push_function([=]() {
		Aero::Resource::AssetManager::Get().cleanup();
	});

	create_render_semaphores();

	_sceneRenderer = std::make_unique<Aero::Renderer::SceneRenderer>();
	_sceneRenderer->init(_renderDevice.get(), _window->width(), _window->height());

	init_imgui();

	_currentScenePath = "F:/VSproject/AeroEngine/assets/Sponza/glTF/Sponza.gltf";
	//_currentScenePath = "F:/VSproject/AeroEngine/assets/Bistro/BistroExterior.gltf";
	auto& am = Aero::Resource::AssetManager::Get();
	if (am.load_scene("main", _currentScenePath)) {
		GpuScene* gpuScene = am.get_scene("main");
		if (gpuScene) {
			_sceneRenderer->submit_scene(*gpuScene, &_reloadStatus);
		} else {
			std::cerr << "[AeroEngine] CRITICAL: get_scene returned null!" << std::endl;
			_reloadStatus = "Failed to get GPU scene";
		}
	} else {
		std::cerr << "[AeroEngine] CRITICAL: Failed to load startup scene!" << std::endl;
		_reloadStatus = "Failed to load startup scene";
	}

	_isInitialized = true;

	std::cout << "[AeroEngine] Initialization complete." << std::endl;
}

void AeroEngine::process_reload_requests() {
	if (!_pendingSceneReload && !_pendingShaderReload) {
		return;
	}

	// Shader 重载：先编译再拆场景，避免编译失败后 _currentScene 悬空
	if (_pendingShaderReload) {
		if (!_sceneRenderer->reload_shaders_and_scene_dry_run(&_reloadStatus)) {
			// 编译失败，保持旧场景不动
			_pendingShaderReload = false;
			return;
		}
	}

	auto& am = Aero::Resource::AssetManager::Get();
	vkDeviceWaitIdle(_renderDevice->get_device());
	am.unload_scene("main");

	if (!am.load_scene("main", _currentScenePath)) {
		_reloadStatus = "Scene reload failed";
		_pendingShaderReload = false;
		_pendingSceneReload = false;
		return;
	}

	GpuScene* gpuScene = am.get_scene("main");
	if (!gpuScene) {
		_reloadStatus = "Scene reload failed - no GPU scene";
		_pendingShaderReload = false;
		_pendingSceneReload = false;
		return;
	}

	if (_pendingShaderReload) {
		_reloadStatus = _sceneRenderer->reload_shaders_and_scene(*gpuScene, _window->width(), _window->height(), &_reloadStatus)
		                    ? "Shaders reloaded"
		                    : _reloadStatus;
	} else if (_pendingSceneReload) {
		_reloadStatus = _sceneRenderer->reload_scene(*gpuScene, _window->width(), _window->height(), &_reloadStatus)
		                    ? "Scene reloaded"
		                    : _reloadStatus;
	}

	_pendingShaderReload = false;
	_pendingSceneReload = false;
}

void AeroEngine::create_render_semaphores() {
	destroy_render_semaphores();

	_renderSemaphores.resize(_renderDevice->get_swapchain_images().size());
	VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	for (size_t i = 0; i < _renderSemaphores.size(); i++) {
		VK_CHECK(vkCreateSemaphore(_renderDevice->get_device(), &semaphoreInfo, nullptr, &_renderSemaphores[i]));
	}
}

void AeroEngine::destroy_render_semaphores() {
	if (!_renderDevice) {
		_renderSemaphores.clear();
		return;
	}

	for (VkSemaphore sem : _renderSemaphores) {
		if (sem != VK_NULL_HANDLE) {
			vkDestroySemaphore(_renderDevice->get_device(), sem, nullptr);
		}
	}
	_renderSemaphores.clear();
}

void AeroEngine::recreate_swapchain() {
	_renderDevice->recreate_swapchain(_window.get());
	create_render_semaphores();
	_sceneRenderer->recreate_render_targets(_window->width(), _window->height());
	ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(_renderDevice->get_swapchain_images().size()));
	_window->reset_resize_flag();
}

void AeroEngine::run() {

	while (!_window->should_close()) {
		_window->poll_events();
		process_input();
		draw();
	}
}

void AeroEngine::cleanup() {
	if (_isInitialized) {
		vkDeviceWaitIdle(_renderDevice->get_device());

		_sceneRenderer->cleanup();
		destroy_render_semaphores();
		_mainDeletionQueue.flush();

		//_window.reset();

		_isInitialized = false;
	}
}

void AeroEngine::process_input() {
	float dt = _window->get_delta_time();
	bool isSprint = _window->is_key_down(Aero::Key::LeftShift);

	// ��������ƶ�
	if (_window->is_key_down(Aero::Key::W))
		_camera.ProcessKeyboard(CameraMovement::FORWARD, dt, isSprint);
	if (_window->is_key_down(Aero::Key::S))
		_camera.ProcessKeyboard(CameraMovement::BACKWARD, dt, isSprint);
	if (_window->is_key_down(Aero::Key::A))
		_camera.ProcessKeyboard(CameraMovement::LEFT, dt, isSprint);
	if (_window->is_key_down(Aero::Key::D))
		_camera.ProcessKeyboard(CameraMovement::RIGHT, dt, isSprint);
	if (_window->is_key_down(Aero::Key::Space))
		_camera.ProcessKeyboard(CameraMovement::UP, dt, isSprint);
	if (_window->is_key_down(Aero::Key::LeftControl))
		_camera.ProcessKeyboard(CameraMovement::DOWN, dt, isSprint);

	if (_window->is_mouse_button_down(Aero::Mouse::Right)) {
		_window->set_cursor_mode(true);
		glm::vec2 delta = _window->get_mouse_delta();
		_camera.ProcessMouseMovement(delta.x, delta.y);
	} else {
		_window->set_cursor_mode(false);
	}
}

void AeroEngine::init_imgui() {
	VkFormat swapchainImageFormat = _renderDevice->get_swapchain_format();
	uint32_t swapchainImageCount = static_cast<uint32_t>(_renderDevice->get_swapchain_images().size());

	VkDescriptorPoolSize pool_sizes[] = {
	    {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
	    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
	    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
	    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

	VkDescriptorPoolCreateInfo pool_info{};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t) std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VK_CHECK(vkCreateDescriptorPool(_renderDevice->get_device(), &pool_info, nullptr, &_imguiPool));

	ImGui::CreateContext();
	ImGui_ImplGlfw_InitForVulkan(_window->handle(), true);

	ImGui_ImplVulkan_InitInfo init_info{};
	init_info.Instance = _renderDevice->get_instance();
	init_info.PhysicalDevice = _renderDevice->get_gpu();
	init_info.Device = _renderDevice->get_device();
	init_info.QueueFamily = _renderDevice->get_graphics_queue_family();
	init_info.Queue = _renderDevice->get_graphics_queue();
	init_info.DescriptorPool = _imguiPool;
	init_info.MinImageCount = swapchainImageCount;
	init_info.ImageCount = swapchainImageCount;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.UseDynamicRendering = true;

	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {};
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainImageFormat;

	ImGui_ImplVulkan_Init(&init_info);

	_mainDeletionQueue.push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		vkDestroyDescriptorPool(_renderDevice->get_device(), _imguiPool, nullptr);
	});
}

void AeroEngine::draw() {
	auto cpuFrameStart = std::chrono::high_resolution_clock::now();

	if (_window->was_resized()) {
		recreate_swapchain();
		return;
	}

	auto& currentFrame = _renderDevice->get_current_frame();
	VK_CHECK(vkWaitForFences(_renderDevice->get_device(), 1, &currentFrame.renderFence, true, 1000000000));
	process_reload_requests();

	if (currentFrame.hasValidTimestamps) {
		std::array<uint64_t, Aero::RHI::GPU_TIMESTAMP_QUERY_COUNT> gpuTimestamps{};
		VkResult timestampResult = vkGetQueryPoolResults(
		    _renderDevice->get_device(),
		    currentFrame.timestampQueryPool,
		    0,
		    Aero::RHI::GPU_TIMESTAMP_QUERY_COUNT,
		    sizeof(gpuTimestamps),
		    gpuTimestamps.data(),
		    sizeof(uint64_t),
		    VK_QUERY_RESULT_64_BIT);
		if (timestampResult == VK_SUCCESS) {
			const float nsToMs = 1.0e-6f;
			const float timestampPeriod = _renderDevice->get_timestamp_period_ns();
			_perfStats.gpuFrameMs = static_cast<float>(gpuTimestamps[2] - gpuTimestamps[0]) * timestampPeriod * nsToMs;
			_perfStats.cullingGpuMs = _useGPUDriven
			                              ? static_cast<float>(gpuTimestamps[1] - gpuTimestamps[0]) * timestampPeriod * nsToMs
			                              : 0.0f;
		}
	}

	const auto uploadStats = Aero::Resource::AssetManager::Get().get_upload_stats();
	_perfStats.uploadCpuMs = static_cast<float>(uploadStats.lastSubmitCpuMs);
	_perfStats.uploadBytes = uploadStats.lastSubmittedBytes;

	VK_CHECK(vkResetFences(_renderDevice->get_device(), 1, &currentFrame.renderFence));

	// --- 1. UI �߼� ---
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("AeroEngine Debug");

	// ��ʷ֡ʱ�����ݣ����ڻ�������ͼ
	static float frameTimes[120] = {0};
	static int frameTimeOffset = 0;

	static float fpsTimer = 0.0f;

	fpsTimer += ImGui::GetIO().DeltaTime;
	if (fpsTimer > 1.0f) { // ˢ�¿�һ�㣬��ͼ����˿��
		_perfStats.displayFps = ImGui::GetIO().Framerate;
		_perfStats.displayMs = 1000.0f / _perfStats.displayFps;
		fpsTimer = 0.0f;
		_displayPerfStats = _perfStats;

		frameTimes[frameTimeOffset] = _displayPerfStats.displayMs;
		frameTimeOffset = (frameTimeOffset + 1) % 120;
	}

	ImGui::Text("Performance: %.3f ms/frame (%.1f FPS)", _displayPerfStats.displayMs, _displayPerfStats.displayFps);
	ImGui::PlotLines("##FrameTime", frameTimes, 120, frameTimeOffset, "Frame Time (ms)", 0.0f, 15.0f, ImVec2(0, 80));
	ImGui::Text("CPU Frame: %.3f ms", _displayPerfStats.cpuFrameMs);
	ImGui::Text("GPU Frame: %.3f ms", _displayPerfStats.gpuFrameMs);
	ImGui::Text("GPU Culling: %.3f ms", _displayPerfStats.cullingGpuMs);
	ImGui::Text("Upload Submit CPU: %.3f ms (%llu bytes)", _displayPerfStats.uploadCpuMs, static_cast<unsigned long long>(_displayPerfStats.uploadBytes));
	ImGui::Separator();

	const auto& sceneStats = _sceneRenderer->get_scene_stats();
	ImGui::Text("Scene Stats");
	ImGui::Text("Meshes: %u", sceneStats.meshCount);
	ImGui::Text("SubMeshes: %u", sceneStats.submeshCount);
	ImGui::Text("Materials: %u", sceneStats.materialCount);
	ImGui::Text("Textures: %u", sceneStats.textureCount);
	ImGui::Text("Vertices: %u", sceneStats.vertexCount);
	ImGui::Text("Indices: %u", sceneStats.indexCount);
	ImGui::Separator();

	ImGui::Text("VSync: %s", (_renderDevice->get_swapchain_format() == VK_PRESENT_MODE_FIFO_KHR) ? "ON (FIFO)" : "OFF (MAILBOX)");
	ImGui::Separator();

	ImGui::Text("Reload");
	if (ImGui::Button("Reload Scene")) {
		_pendingSceneReload = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload Shaders")) {
		_pendingShaderReload = true;
	}
	ImGui::Text("Status: %s", _reloadStatus.c_str());
	ImGui::Separator();

	ImGui::Text("Pipeline Architecture");
	ImGui::Checkbox("Enable GPU-Driven Rendering", &_useGPUDriven);
	if (_useGPUDriven) {
		ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Mode: GPU Indirect Draw + Compute Culling");
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Mode: CPU Loop Submission (No Culling)");
	}
	ImGui::Separator();
	// ------------------------------------

	ImGui::Text("Camera Settings");
	ImGui::SliderFloat("Speed", &_camera.MovementSpeed, 1.0f, 50.0f);
	ImGui::SliderFloat("Sensitivity", &_camera.MouseSensitivity, 0.01f, 1.0f);
	ImGui::Text("Position: (%.1f, %.1f, %.1f)", _camera.Position.x, _camera.Position.y, _camera.Position.z);

	ImGui::End();
	ImGui::Render();

	// --- 2. ��ȡ��һ֡ͼ�� ---
	uint32_t swapchainImageIndex;
	VkResult acquireResult = vkAcquireNextImageKHR(_renderDevice->get_device(), _renderDevice->get_swapchain(), 1000000000, _renderDevice->get_current_frame().swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
		recreate_swapchain();
		return;
	}
	VK_CHECK(acquireResult);

	VkCommandBuffer cmd = currentFrame.mainCommandBuffer;
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	vkCmdResetQueryPool(cmd, currentFrame.timestampQueryPool, 0, Aero::RHI::GPU_TIMESTAMP_QUERY_COUNT);
	vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, currentFrame.timestampQueryPool, 0);

	VkImage swapchainImg = _renderDevice->get_swapchain_images()[swapchainImageIndex];
	VkImageView swapchainView = _renderDevice->get_swapchain_image_views()[swapchainImageIndex];

	// --- 3. ������Ⱦ Pass ---
	vkinit::transition_image(cmd, swapchainImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// ���ĵ��ã��������صĻ��ƶ����� Renderer
	_sceneRenderer->draw(cmd, swapchainView, _camera, _window->width(), _window->height(), _useGPUDriven, currentFrame.timestampQueryPool);

	// --- 4. UI ��Ⱦ Pass (������ɫ������LOAD_OP_LOAD ��������л���) ---
	VkExtent2D currentExtent = {_window->width(), _window->height()};
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(swapchainView, nullptr);
	VkRenderingInfo renderInfo = vkinit::rendering_info(currentExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	vkCmdEndRendering(cmd);
	vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, currentFrame.timestampQueryPool, 2);

	// --- 5. �ύ����� ---
	vkinit::transition_image(cmd, swapchainImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd};
	VkSemaphoreSubmitInfo waitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = currentFrame.swapchainSemaphore, .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSemaphoreSubmitInfo signalInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = _renderSemaphores[swapchainImageIndex], .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT};

	VkSubmitInfo2 submit{
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
	    .waitSemaphoreInfoCount = 1,
	    .pWaitSemaphoreInfos = &waitInfo,
	    .commandBufferInfoCount = 1, // ������ Signal ǰ��
	    .pCommandBufferInfos = &cmdinfo, // ������ Signal ǰ��
	    .signalSemaphoreInfoCount = 1,
	    .pSignalSemaphoreInfos = &signalInfo};
	VK_CHECK(vkQueueSubmit2(_renderDevice->get_graphics_queue(), 1, &submit, currentFrame.renderFence));
	currentFrame.hasValidTimestamps = true;

	VkSwapchainKHR swapchain = _renderDevice->get_swapchain();
	VkPresentInfoKHR presentInfo{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &_renderSemaphores[swapchainImageIndex], .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &swapchainImageIndex};
	VkResult presentResult = vkQueuePresentKHR(_renderDevice->get_graphics_queue(), &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || _window->was_resized()) {
		recreate_swapchain();
		return;
	}
	VK_CHECK(presentResult);

	auto cpuFrameEnd = std::chrono::high_resolution_clock::now();
	_perfStats.cpuFrameMs = static_cast<float>(std::chrono::duration<double, std::milli>(cpuFrameEnd - cpuFrameStart).count());

	_renderDevice->advance_frame();
}
