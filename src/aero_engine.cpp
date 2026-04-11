#include "aero_engine.h"
#include <iostream>
#include <optional>
#include "vk_initializers.h"
#include "gltf_loader.h"
#include "Core/KeyCodes.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

AeroEngine& AeroEngine::Get() {
	static AeroEngine engine;
	return engine;
}

void AeroEngine::init() {

	_window = std::make_unique<Aero::Window>(Aero::Window::Specs{ 1280, 720, "AeroEngine v0.1" });

	_renderDevice = std::make_unique<Aero::RHI::VulkanDevice>();
	_renderDevice->init(_window.get(), _mainDeletionQueue);

	_renderSemaphores.resize(_renderDevice->get_swapchain_images().size());
	VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	for (size_t i = 0; i < _renderSemaphores.size(); i++) {
		VK_CHECK(vkCreateSemaphore(_renderDevice->get_device(), &semaphoreInfo, nullptr, &_renderSemaphores[i]));
	}
	_mainDeletionQueue.push_function([=]() {
		for (VkSemaphore sem : _renderSemaphores) {
			vkDestroySemaphore(_renderDevice->get_device(), sem, nullptr);
		}
		});

	_sceneRenderer = std::make_unique<Aero::Renderer::SceneRenderer>();
	_sceneRenderer->init(_renderDevice.get(),_window->width(),_window->height());

	init_imgui();

	std::string modelPath = "F:/VSproject/AeroEngine/assets/Sponza/glTF/Sponza.gltf";
	//std::string modelPath = "F:/VSproject/AeroEngine/assets/Bistro/BistroExterior.gltf";
	std::optional<SceneData> sceneOpt = GLTFLoader::load_gltf(modelPath);
	if (sceneOpt.has_value()) {
		_sceneRenderer->upload_scene(sceneOpt.value());
	}
	else {
		std::cerr << "[AeroEngine] CRITICAL: Failed to load startup scene!" << std::endl;
	}

	_isInitialized = true;
	
	std::cout << "[AeroEngine] Initialization complete." << std::endl;
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
		_mainDeletionQueue.flush();

		//_window.reset();

		_isInitialized = false;
	}
}

void AeroEngine::process_input() {
	float dt = _window->get_delta_time();
	bool isSprint = _window->is_key_down(Aero::Key::LeftShift);

	// 处理相机移动
	if (_window->is_key_down(Aero::Key::W)) _camera.ProcessKeyboard(CameraMovement::FORWARD, dt, isSprint);
	if (_window->is_key_down(Aero::Key::S)) _camera.ProcessKeyboard(CameraMovement::BACKWARD, dt, isSprint);
	if (_window->is_key_down(Aero::Key::A)) _camera.ProcessKeyboard(CameraMovement::LEFT, dt, isSprint);
	if (_window->is_key_down(Aero::Key::D)) _camera.ProcessKeyboard(CameraMovement::RIGHT, dt, isSprint);
	if (_window->is_key_down(Aero::Key::Space)) _camera.ProcessKeyboard(CameraMovement::UP, dt, isSprint);
	if (_window->is_key_down(Aero::Key::LeftControl)) _camera.ProcessKeyboard(CameraMovement::DOWN, dt, isSprint);


	if (_window->is_mouse_button_down(Aero::Mouse::Right)) {
		_window->set_cursor_mode(true);
		glm::vec2 delta = _window->get_mouse_delta();
		_camera.ProcessMouseMovement(delta.x, delta.y);
	}
	else {
		_window->set_cursor_mode(false);
	}
}

void AeroEngine::init_imgui() {
	VkFormat swapchainImageFormat = _renderDevice->get_swapchain_format();

	VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo pool_info{};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
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
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
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
	VK_CHECK(vkWaitForFences(_renderDevice->get_device(), 1, &_renderDevice->get_current_frame().renderFence, true, 1000000000));
	VK_CHECK(vkResetFences(_renderDevice->get_device(), 1, &_renderDevice->get_current_frame().renderFence));

	// --- 1. UI 逻辑 ---
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("AeroEngine Debug");

	// 历史帧时间数据，用于绘制折线图
	static float frameTimes[120] = { 0 };
	static int frameTimeOffset = 0;

	static float fpsTimer = 0.0f;
	static float displayFps = 0.0f;
	static float displayMs = 0.0f;

	fpsTimer += ImGui::GetIO().DeltaTime;
	if (fpsTimer > 1.0f) { // 刷新快一点，让图表更丝滑
		displayFps = ImGui::GetIO().Framerate;
		displayMs = 1000.0f / displayFps;
		fpsTimer = 0.0f;

		frameTimes[frameTimeOffset] = displayMs;
		frameTimeOffset = (frameTimeOffset + 1) % 120;
	}

	ImGui::Text("Performance: %.3f ms/frame (%.1f FPS)", displayMs, displayFps);
	ImGui::PlotLines("##FrameTime", frameTimes, 120, frameTimeOffset, "Frame Time (ms)", 0.0f, 15.0f, ImVec2(0, 80));
	ImGui::Separator();

	ImGui::Text("VSync: %s", (_renderDevice->get_swapchain_format() == VK_PRESENT_MODE_FIFO_KHR) ? "ON (FIFO)" : "OFF (MAILBOX)");
	ImGui::Separator();

	ImGui::Text("Pipeline Architecture");
	ImGui::Checkbox("Enable GPU-Driven Rendering", &_useGPUDriven);
	if (_useGPUDriven) {
		ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Mode: GPU Indirect Draw + Compute Culling");
	}
	else {
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

	// --- 2. 获取下一帧图像 ---
	uint32_t swapchainImageIndex;
	VkResult acquireResult = vkAcquireNextImageKHR(_renderDevice->get_device(), _renderDevice->get_swapchain(), 1000000000, _renderDevice->get_current_frame().swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) return;

	VkCommandBuffer cmd = _renderDevice->get_current_frame().mainCommandBuffer;
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkImage swapchainImg = _renderDevice->get_swapchain_images()[swapchainImageIndex];
	VkImageView swapchainView = _renderDevice->get_swapchain_image_views()[swapchainImageIndex];

	// --- 3. 场景渲染 Pass ---
	vkinit::transition_image(cmd, swapchainImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// 核心调用：所有像素的绘制都交给 Renderer
	_sceneRenderer->draw(cmd, swapchainView, _camera, _window->width(), _window->height(), _useGPUDriven);

	// --- 4. UI 渲染 Pass (复用颜色附件，LOAD_OP_LOAD 不清空已有画面) ---
	VkExtent2D currentExtent = { _window->width(), _window->height() };
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(swapchainView, nullptr);
	VkRenderingInfo renderInfo = vkinit::rendering_info(currentExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	vkCmdEndRendering(cmd);

	// --- 5. 提交与呈现 ---
	vkinit::transition_image(cmd, swapchainImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd };
	VkSemaphoreSubmitInfo waitInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = _renderDevice->get_current_frame().swapchainSemaphore, .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSemaphoreSubmitInfo signalInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = _renderSemaphores[swapchainImageIndex], .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT };

	VkSubmitInfo2 submit{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &waitInfo,
		.commandBufferInfoCount = 1,      // 必须在 Signal 前面
		.pCommandBufferInfos = &cmdinfo,  // 必须在 Signal 前面
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = &signalInfo
	};
	VK_CHECK(vkQueueSubmit2(_renderDevice->get_graphics_queue(), 1, &submit, _renderDevice->get_current_frame().renderFence));

	VkSwapchainKHR swapchain = _renderDevice->get_swapchain();
	VkPresentInfoKHR presentInfo{ .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &_renderSemaphores[swapchainImageIndex], .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &swapchainImageIndex };
	vkQueuePresentKHR(_renderDevice->get_graphics_queue(), &presentInfo);

	_renderDevice->advance_frame();
}