#include "aero_engine.h"
#include <iostream>
#include <optional>
#include <VkBootstrap.h>
#include "vk_initializers.h"
#include <vk_pipelines.h>
#include <stb_image.h>
#include "gltf_loader.h"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <memory>
#include <array>


#include "Core/KeyCodes.h"

struct ComputePushConstants {
	glm::vec4 planes[6];    // 6个视锥平面方程 (Ax + By + Cz + D = 0)
	uint32_t instanceCount;
};

std::array<glm::vec4, 6> get_frustum_planes(const glm::mat4& viewProj) {
	std::array<glm::vec4, 6> planes;

	// glm 是列主序，为了套用标准行主序提取公式，我们先求转置
	glm::mat4 M = glm::transpose(viewProj);

	planes[0] = M[3] + M[0]; // Left
	planes[1] = M[3] - M[0]; // Right
	planes[2] = M[3] + M[1]; // Bottom
	planes[3] = M[3] - M[1]; // Top
	planes[4] = M[2];        // Near (在 Reverse-Z 和 Vulkan ZO 下依然适用)
	planes[5] = M[3] - M[2]; // Far

	//归一化平面法线，这样 plane.w 的物理意义就是点到原点的距离
	for (auto& p : planes) {
		float length = glm::length(glm::vec3(p.x, p.y, p.z));
		p /= length;
	}
	return planes;
}

struct MeshPushConstants {
	glm::mat4 render_matrix;
	uint32_t material_id;
};

AeroEngine& AeroEngine::Get() {
	static AeroEngine engine;
	return engine;
}

void AeroEngine::init() {

	_window = std::make_unique<Aero::Window>(Aero::Window::Specs{ 1280, 720, "AeroEngine v0.1" });

	init_vulkan();

	init_swapchain();

	init_allocator();

	init_commands();

	init_sync_structures();

	init_bindless_descriptor();

	init_depth_image();

	init_pipelines();

	init_imgui();

	std::string modelPath = "F:/VSproject/AeroEngine/assets/Sponza/glTF/Sponza.gltf";
	//std::string modelPath = "F:/VSproject/AeroEngine/assets/Bistro/BistroExterior.gltf";
	std::optional<SceneData> sceneOpt = GLTFLoader::load_gltf(modelPath);
	if (sceneOpt.has_value()) {
		upload_scene_data(sceneOpt.value());
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
		vkDeviceWaitIdle(_vkContext.device);
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


	// 处理鼠标视角
	if (_window->is_mouse_button_down(Aero::Mouse::Right)) {
		_window->set_cursor_mode(true); // 锁定
		glm::vec2 delta = _window->get_mouse_delta();
		_camera.ProcessMouseMovement(delta.x, delta.y);
	}
	else {
		_window->set_cursor_mode(false); // 释放
	}
}

void AeroEngine::init_vulkan() {
	_vkContext.init(_window->handle(), _mainDeletionQueue);

	VkSamplerCreateInfo sampInfo{};
	sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampInfo.magFilter = VK_FILTER_LINEAR;
	sampInfo.minFilter = VK_FILTER_LINEAR;
	sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampInfo.maxAnisotropy = 1.0f;

	sampInfo.minLod = 0.0f;
	sampInfo.maxLod = VK_LOD_CLAMP_NONE;
	sampInfo.mipLodBias = 0.0f;
	VK_CHECK(vkCreateSampler(_vkContext.device, &sampInfo, nullptr, &_defaultSamplerLinear));

	_mainDeletionQueue.push_function([=]() {
		vkDestroySampler(_vkContext.device, _defaultSamplerLinear, nullptr);
		});
}

void AeroEngine::init_swapchain() {
	vkb::SwapchainBuilder swapchainBuilder{ _vkContext.chosenGPU, _vkContext.device, _vkContext.surface };

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
		.set_desired_extent(_window->width(), _window->height())
		.build()
		.value();

	_swapchain = vkbSwapchain.swapchain;
	_swapchainImageFormat = vkbSwapchain.image_format;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

	_renderSemaphores.resize(_swapchainImages.size());
	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	for (size_t i = 0; i < _swapchainImages.size(); i++) {
		VK_CHECK(vkCreateSemaphore(_vkContext.device, &semaphoreInfo, nullptr, &_renderSemaphores[i]));
	}

	_mainDeletionQueue.push_function([=]() {
		for (VkSemaphore sem : _renderSemaphores) {
			vkDestroySemaphore(_vkContext.device, sem, nullptr);
		}
		for (VkImageView view : _swapchainImageViews) {
			vkDestroyImageView(_vkContext.device, view, nullptr);
		}
		vkDestroySwapchainKHR(_vkContext.device, _swapchain, nullptr);
		std::cout << "[AeroEngine] Swapchain destroyed." << std::endl;
		});
}

void AeroEngine::init_allocator() {
	//because we used volk
	VmaVulkanFunctions vulkanFunctions = {};
	vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
	vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
	vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
	vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
	vulkanFunctions.vkFreeMemory = vkFreeMemory;
	vulkanFunctions.vkMapMemory = vkMapMemory;
	vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
	vulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
	vulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
	vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
	vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
	vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
	vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
	vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
	vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
	vulkanFunctions.vkCreateImage = vkCreateImage;
	vulkanFunctions.vkDestroyImage = vkDestroyImage;
	vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _vkContext.chosenGPU;
	allocatorInfo.device = _vkContext.device;
	allocatorInfo.instance = _vkContext.instance;

	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	allocatorInfo.pVulkanFunctions = &vulkanFunctions;

	VK_CHECK(vmaCreateAllocator(&allocatorInfo, &_allocator));

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyAllocator(_allocator);
		std::cout << "[AeroEngine] VMA Allocator destroyed." << std::endl;
		});
}

void AeroEngine::init_commands() {
	VkCommandPoolCreateInfo commandPoolInfo{};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.pNext = nullptr;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolInfo.queueFamilyIndex = _vkContext.graphicsQueueFamily;

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateCommandPool(_vkContext.device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		VkCommandBufferAllocateInfo cmdAllocInfo{};
		cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAllocInfo.pNext = nullptr;
		cmdAllocInfo.commandPool = _frames[i]._commandPool;
		cmdAllocInfo.commandBufferCount = 1;
		cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		VK_CHECK(vkAllocateCommandBuffers(_vkContext.device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(_vkContext.device, _frames[i]._commandPool, nullptr);
			});
	}
}

void AeroEngine::init_sync_structures() {
	VkFenceCreateInfo fenceCreateInfo{};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkSemaphoreCreateInfo semaphoreCreateInfo{};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = 0;

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(_vkContext.device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		VK_CHECK(vkCreateSemaphore(_vkContext.device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyFence(_vkContext.device, _frames[i]._renderFence, nullptr);
			vkDestroySemaphore(_vkContext.device, _frames[i]._swapchainSemaphore, nullptr);
			});
	}
}

void AeroEngine::init_pipelines() {
	VkShaderModule triangleFragShader;
	if (!vkutil::load_shader_module("shaders/mesh.frag.spv", _vkContext.device, &triangleFragShader)) {
		std::cout << "Error when building the triangle fragment shader module" << std::endl;
	}
	VkShaderModule triangleVertShader;
	if (!vkutil::load_shader_module("shaders/mesh.vert.spv", _vkContext.device, &triangleVertShader)) {
		std::cout << "Error when building the triangle vertex shader module" << std::endl;
	}

	VkPushConstantRange pushConstant{};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(glm::mat4);
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	//创建 Pipeline Layout
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &_globalSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

	VK_CHECK(vkCreatePipelineLayout(_vkContext.device, &pipelineLayoutInfo, nullptr, &_trianglePipelineLayout));

	//配置 Pipeline Builder
	PipelineBuilder builder;
	builder._pipelineLayout = _trianglePipelineLayout;

	//顶点着色器阶段
	VkPipelineShaderStageCreateInfo vertStage{};
	vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertStage.module = triangleVertShader;
	vertStage.pName = "main";
	builder._shaderStages.push_back(vertStage);

	//片段着色器阶段
	VkPipelineShaderStageCreateInfo fragStage{};
	fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragStage.module = triangleFragShader;
	fragStage.pName = "main";
	builder._shaderStages.push_back(fragStage);

	builder._inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	builder._inputAssembly.primitiveRestartEnable = VK_FALSE;

	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	builder._vertexInputInfo = {};
	builder._vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	builder._vertexInputInfo.vertexBindingDescriptionCount = 1;
	builder._vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	builder._vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
	builder._vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	builder._depthStencil.depthTestEnable = VK_TRUE;
	builder._depthStencil.depthWriteEnable = VK_TRUE;
	builder._depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;

	builder._rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	builder._rasterizer.lineWidth = 1.0f;
	builder._rasterizer.cullMode = VK_CULL_MODE_NONE;
	builder._rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

	builder._multisampling.sampleShadingEnable = VK_FALSE;
	builder._multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	builder._colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	builder._colorBlendAttachment.blendEnable = VK_FALSE;

	builder._renderInfo.colorAttachmentCount = 1;
	builder._renderInfo.pColorAttachmentFormats = &_swapchainImageFormat;
	builder._renderInfo.depthAttachmentFormat = _depthImageFormat;

	_trianglePipeline = builder.build_pipeline(_vkContext.device);

	vkDestroyShaderModule(_vkContext.device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, triangleVertShader, nullptr);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(_vkContext.device, _trianglePipeline, nullptr);
		vkDestroyPipelineLayout(_vkContext.device, _trianglePipelineLayout, nullptr);
		});

	//compute culling pipeline
	VkShaderModule computeShader;
	if (!vkutil::load_shader_module("shaders/culling.comp.spv", _vkContext.device, &computeShader)) {
		std::cerr << "Error when building the culling compute shader module" << std::endl;
	}
	VkPushConstantRange computePushConstant{};
	computePushConstant.offset = 0;
	computePushConstant.size = sizeof(ComputePushConstants);
	computePushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkPipelineLayoutCreateInfo computeLayoutInfo{};
	computeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayoutInfo.setLayoutCount = 1;
	computeLayoutInfo.pSetLayouts = &_globalSetLayout;
	computeLayoutInfo.pushConstantRangeCount = 1;
	computeLayoutInfo.pPushConstantRanges = &computePushConstant;

	VK_CHECK(vkCreatePipelineLayout(_vkContext.device, &computeLayoutInfo, nullptr, &_cullingPipelineLayout));

	VkComputePipelineCreateInfo computePipelineInfo{};
	computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineInfo.layout = _cullingPipelineLayout;
	computePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	computePipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	computePipelineInfo.stage.module = computeShader;
	computePipelineInfo.stage.pName = "main";

	VK_CHECK(vkCreateComputePipelines(_vkContext.device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &_cullingPipeline));

	vkDestroyShaderModule(_vkContext.device, computeShader, nullptr);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(_vkContext.device, _cullingPipeline, nullptr);
		vkDestroyPipelineLayout(_vkContext.device, _cullingPipelineLayout, nullptr);
		});
}

void AeroEngine::init_imgui() {
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

	VK_CHECK(vkCreateDescriptorPool(_vkContext.device, &pool_info, nullptr, &_imguiPool));

	ImGui::CreateContext();
	ImGui_ImplGlfw_InitForVulkan(_window->handle(), true);

	ImGui_ImplVulkan_InitInfo init_info{};
	init_info.Instance = _vkContext.instance;
	init_info.PhysicalDevice = _vkContext.chosenGPU;
	init_info.Device = _vkContext.device;
	init_info.QueueFamily = _vkContext.graphicsQueueFamily;
	init_info.Queue = _vkContext.graphicsQueue;
	init_info.DescriptorPool = _imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.UseDynamicRendering = true;

	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {};
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;

	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.depthAttachmentFormat = _depthImageFormat;

	ImGui_ImplVulkan_Init(&init_info);

	_mainDeletionQueue.push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		vkDestroyDescriptorPool(_vkContext.device, _imguiPool, nullptr);
		});
}

void AeroEngine::draw() {
	VK_CHECK(vkWaitForFences(_vkContext.device, 1, &get_current_frame()._renderFence, true, 1000000000));
	VK_CHECK(vkResetFences(_vkContext.device, 1, &get_current_frame()._renderFence));

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

	ImGui::Text("VSync: %s", (_swapchainImageFormat == VK_PRESENT_MODE_FIFO_KHR) ? "ON (FIFO)" : "OFF (MAILBOX)");
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

	uint32_t swapchainImageIndex;
	VkResult acquireResult = vkAcquireNextImageKHR(_vkContext.device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
		return;
	}
	else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
		VK_CHECK(acquireResult);
	}

	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// 提前计算视锥体矩阵，供 Compute 和 Graphics 共同使用
	glm::mat4 view = _camera.GetViewMatrix();
	glm::mat4 proj = glm::perspectiveZO(glm::radians(_camera.Fov), (float)_window->width() / (float)_window->height(), 10000.0f, 0.1f);
	proj[1][1] *= -1; // Vulkan Y轴翻转
	glm::mat4 viewProj = proj * view;

	// ================= 阶段 1：Compute Culling =================
	// 仅在开启 GPU Driven 模式时执行视锥体剔除
	if (_instanceCount > 0 && _useGPUDriven) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cullingPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cullingPipelineLayout, 0, 1, &_globalDescriptorSet, 0, nullptr);
		ComputePushConstants computePush{};
		computePush.instanceCount = _instanceCount;
		auto planes = get_frustum_planes(viewProj);
		for (int i = 0; i < 6; i++) {
			computePush.planes[i] = planes[i];
		}
		vkCmdPushConstants(cmd, _cullingPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &computePush);

		// 派发 Compute Shader (每组处理 256 个实例)
		uint32_t groupCount = (_instanceCount + 255) / 256;
		vkCmdDispatch(cmd, groupCount, 1, 1);

		// 插入内存屏障，确保 Compute 写入完成后，Graphics 管线才读取 Indirect Buffer
		VkBufferMemoryBarrier indirectBarrier{};
		indirectBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		indirectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		indirectBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		indirectBarrier.buffer = _drawIndirectBuffer.buffer;
		indirectBarrier.offset = 0;
		indirectBarrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
			0, 0, nullptr, 1, &indirectBarrier, 0, nullptr);
	}

	// ================= 阶段 2：Graphics Rendering =================
	vkinit::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkClearValue clearValue;
	clearValue.color = { {0.05f, 0.05f, 0.08f, 1.0f} };
	VkExtent2D currentExtent = { _window->width(), _window->height() };

	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_swapchainImageViews[swapchainImageIndex], &clearValue);
	VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_depthImage.view);
	VkRenderingInfo renderInfo = vkinit::rendering_info(currentExtent, &colorAttachment, &depthAttachment);

	vkCmdBeginRendering(cmd, &renderInfo);

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)currentExtent.width;
	viewport.height = (float)currentExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = currentExtent;
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);

	if (_instanceCount > 0 && _mainMeshBuffers.vertexBuffer.buffer != VK_NULL_HANDLE) {
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipelineLayout, 0, 1, &_globalDescriptorSet, 0, nullptr);

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &_mainMeshBuffers.vertexBuffer.buffer, &offset);
		vkCmdBindIndexBuffer(cmd, _mainMeshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		// 全局推送 ViewProj 矩阵
		vkCmdPushConstants(cmd, _trianglePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4), &viewProj);

		if (_useGPUDriven) {
			// 一键绘制：GPU 自己去读 Indirect Buffer
			vkCmdDrawIndexedIndirect(
				cmd,
				_drawIndirectBuffer.buffer,
				0,                          // buffer 的起始偏移量
				_instanceCount,             // draw 的最大数量
				sizeof(VkDrawIndexedIndirectCommand) // 步长
			);
		}
		else {
			//to test 
			for (uint32_t i = 0; i < _instanceCount; i++) {
				// 利用取模运算，循环获取 1 个 Sponza 内的网格参数
				const SubMesh& submesh = _renderables[i % _renderables.size()];

				// 参数 i 会直接映射为 Vertex Shader 里的 gl_InstanceIndex
				vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.firstIndex, submesh.vertexOffset, i);
			}

			//for (uint32_t i = 0; i < _renderables.size(); i++) {
			//	const SubMesh& submesh = _renderables[i];
			//	vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.firstIndex, submesh.vertexOffset, i);
			//}
		}
	}

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);

	vkinit::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo{};
	cmdinfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdinfo.commandBuffer = cmd;

	VkSemaphoreSubmitInfo waitInfo{};
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitInfo.semaphore = get_current_frame()._swapchainSemaphore;
	waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSemaphoreSubmitInfo signalInfo{};
	signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalInfo.semaphore = _renderSemaphores[swapchainImageIndex];
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

	VkSubmitInfo2 submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.waitSemaphoreInfoCount = 1;
	submit.pWaitSemaphoreInfos = &waitInfo;
	submit.signalSemaphoreInfoCount = 1;
	submit.pSignalSemaphoreInfos = &signalInfo;
	submit.commandBufferInfoCount = 1;
	submit.pCommandBufferInfos = &cmdinfo;

	VK_CHECK(vkQueueSubmit2(_vkContext.graphicsQueue, 1, &submit, get_current_frame()._renderFence));

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &_renderSemaphores[swapchainImageIndex];
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &swapchainImageIndex;

	VkResult presentResult = vkQueuePresentKHR(_vkContext.graphicsQueue, &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
		return;
	}
	else if (presentResult != VK_SUCCESS) {
		VK_CHECK(presentResult);
	}

	_frameNumber++;
}

void AeroEngine::init_depth_image() {
	VkExtent3D depthImageExtent = {
		_window->width(),
		_window->height(),
		1
	};

	// 1. 创建 Depth Image 描述信息
	VkImageCreateInfo dimg_info{};
	dimg_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	dimg_info.imageType = VK_IMAGE_TYPE_2D;
	dimg_info.format = _depthImageFormat;
	dimg_info.extent = depthImageExtent;
	dimg_info.mipLevels = 1;
	dimg_info.arrayLayers = 1;
	dimg_info.samples = VK_SAMPLE_COUNT_1_BIT;
	dimg_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	// 核心：告诉 Vulkan 这是用来做深度附件的
	dimg_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VmaAllocationCreateInfo dimg_allocinfo{};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// 使用 VMA 分配内存并绑定
	VK_CHECK(vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr));

	// 2. 创建 Depth ImageView
	VkImageViewCreateInfo view_info{};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = _depthImage.image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = _depthImageFormat;
	// 核心：告诉 Vulkan 视图读取的是深度切面 (Aspect)
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;

	VK_CHECK(vkCreateImageView(_vkContext.device, &view_info, nullptr, &_depthImage.view));

	immediate_submit([&](VkCommandBuffer cmd) {
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		barrier.image = _depthImage.image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; // 务必指定为深度
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		});

	// 压入全局销毁队列
	_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(_vkContext.device, _depthImage.view, nullptr);
		vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
		});

	std::cout << "[AeroEngine] Depth Image allocated successfully." << std::endl;
}

void AeroEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {
	VkCommandBuffer cmd = _vkContext.m_uploadContext.commandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(cmd, &cmdBeginInfo);
	function(cmd);
	vkEndCommandBuffer(cmd);

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	vkQueueSubmit(_vkContext.graphicsQueue, 1, &submit, _vkContext.m_uploadContext.uploadFence);

	vkWaitForFences(_vkContext.device, 1, &_vkContext.m_uploadContext.uploadFence, VK_TRUE, UINT64_MAX);
	vkResetFences(_vkContext.device, 1, &_vkContext.m_uploadContext.uploadFence);

	vkResetCommandPool(_vkContext.device, _vkContext.m_uploadContext.commandPool, 0);
}

GPUMeshBuffers AeroEngine::upload_mesh_data(const SceneData& scene) {
	GPUMeshBuffers outBuffers;

	const size_t vertexBufferSize = scene.vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = scene.indices.size() * sizeof(uint32_t);
	const size_t totalBufferSize = vertexBufferSize + indexBufferSize;

	VkBufferCreateInfo stagingBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	stagingBufferInfo.size = totalBufferSize;
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo stagingAllocInfo = {};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
	stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	AllocatedBuffer stagingBuffer;
	VmaAllocationInfo stagingAllocResult;
	VK_CHECK(vmaCreateBuffer(_allocator, &stagingBufferInfo, &stagingAllocInfo,
		&stagingBuffer.buffer, &stagingBuffer.allocation, &stagingAllocResult));

	void* mappedData = stagingAllocResult.pMappedData;
	memcpy(mappedData, scene.vertices.data(), vertexBufferSize);
	memcpy(static_cast<char*>(mappedData) + vertexBufferSize, scene.indices.data(), indexBufferSize);

	VkBufferCreateInfo vboInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	vboInfo.size = vertexBufferSize;
	vboInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VkBufferCreateInfo iboInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	iboInfo.size = indexBufferSize;
	iboInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateBuffer(_allocator, &vboInfo, &vmaAllocInfo,
		&outBuffers.vertexBuffer.buffer, &outBuffers.vertexBuffer.allocation, nullptr));

	VK_CHECK(vmaCreateBuffer(_allocator, &iboInfo, &vmaAllocInfo,
		&outBuffers.indexBuffer.buffer, &outBuffers.indexBuffer.allocation, nullptr));

	immediate_submit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy = { 0, 0, vertexBufferSize };
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, outBuffers.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy = { vertexBufferSize, 0, indexBufferSize };
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, outBuffers.indexBuffer.buffer, 1, &indexCopy);
		});

	vmaDestroyBuffer(_allocator, stagingBuffer.buffer, stagingBuffer.allocation);

	return outBuffers;
}

void AeroEngine::init_bindless_descriptor() {
	VkDescriptorSetLayoutBinding materialBind{};
	materialBind.binding = 0;
	materialBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	materialBind.descriptorCount = 1;
	materialBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding textureBind{};
	textureBind.binding = 1;
	textureBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textureBind.descriptorCount = 4096;
	textureBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding instanceBind{};
	instanceBind.binding = 2;
	instanceBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	instanceBind.descriptorCount = 1;
	instanceBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutBinding indirectBind{};
	indirectBind.binding = 3;
	indirectBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	indirectBind.descriptorCount = 1;
	indirectBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutBinding bindings[]{ materialBind, textureBind, instanceBind,indirectBind };

	VkDescriptorBindingFlags bindlessFlags =
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

	VkDescriptorBindingFlags bindingFlags[4] = { 0, bindlessFlags,0,0 };

	VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
	extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	extendedInfo.bindingCount = 4;
	extendedInfo.pBindingFlags = bindingFlags;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = &extendedInfo;
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	layoutInfo.bindingCount = 4;
	layoutInfo.pBindings = bindings;

	VK_CHECK(vkCreateDescriptorSetLayout(_vkContext.device, &layoutInfo, nullptr, &_globalSetLayout));

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = poolSizes;

	VK_CHECK(vkCreateDescriptorPool(_vkContext.device, &poolInfo, nullptr, &_globalDescriptorPool));

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = _globalDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &_globalSetLayout;

	VK_CHECK(vkAllocateDescriptorSets(_vkContext.device, &allocInfo, &_globalDescriptorSet));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyDescriptorPool(_vkContext.device, _globalDescriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(_vkContext.device, _globalSetLayout, nullptr);
		});

	std::cout << "[AeroEngine] Bindless Descriptor Setup Complete." << std::endl;
}

AllocatedBuffer AeroEngine::upload_ssbo_data(size_t bufferSize, const void* data) {
	// 注意这里第一行不用再算 bufferSize 了，直接用传进来的
	VkBufferCreateInfo ssboInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	ssboInfo.size = bufferSize;
	ssboInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	AllocatedBuffer ssboBuffer;
	VK_CHECK(vmaCreateBuffer(_allocator, &ssboInfo, &vmaAllocInfo,
		&ssboBuffer.buffer, &ssboBuffer.allocation, nullptr));

	VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	stagingInfo.size = bufferSize;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo stagingAllocInfo = {};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
	stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	AllocatedBuffer stagingBuffer;
	VmaAllocationInfo stagingAllocResult;
	VK_CHECK(vmaCreateBuffer(_allocator, &stagingInfo, &stagingAllocInfo,
		&stagingBuffer.buffer, &stagingBuffer.allocation, &stagingAllocResult));

	// 使用传入的 data 指针进行拷贝
	memcpy(stagingAllocResult.pMappedData, data, bufferSize);

	immediate_submit([&](VkCommandBuffer cmd) {
		VkBufferCopy copyRegion = { 0, 0, bufferSize };
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, ssboBuffer.buffer, 1, &copyRegion);
		});

	vmaDestroyBuffer(_allocator, stagingBuffer.buffer, stagingBuffer.allocation);

	return ssboBuffer;
}

void AeroEngine::update_global_descriptor_set() {
	VkDescriptorBufferInfo matBufferInfo{};
	matBufferInfo.buffer = _materialBuffer.buffer;
	matBufferInfo.offset = 0;
	matBufferInfo.range = VK_WHOLE_SIZE;

	VkWriteDescriptorSet matWrite{};
	matWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	matWrite.dstSet = _globalDescriptorSet;
	matWrite.dstBinding = 0; //binding 1 for material SSBO
	matWrite.dstArrayElement = 0;
	matWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	matWrite.descriptorCount = 1;
	matWrite.pBufferInfo = &matBufferInfo;

	VkDescriptorBufferInfo instBufferInfo{};
	instBufferInfo.buffer = _instanceBuffer.buffer;
	instBufferInfo.offset = 0;
	instBufferInfo.range = VK_WHOLE_SIZE;

	VkWriteDescriptorSet instWrite{};
	instWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	instWrite.dstSet = _globalDescriptorSet;
	instWrite.dstBinding = 2; //binding 2 for instance SSBO;
	instWrite.dstArrayElement = 0;
	instWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	instWrite.descriptorCount = 1;
	instWrite.pBufferInfo = &instBufferInfo;

	VkDescriptorBufferInfo indirectBufferInfo{};
	indirectBufferInfo.buffer = _drawIndirectBuffer.buffer;
	indirectBufferInfo.offset = 0;
	indirectBufferInfo.range = VK_WHOLE_SIZE;

	VkWriteDescriptorSet indirectWrite{};
	indirectWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	indirectWrite.dstSet = _globalDescriptorSet;
	indirectWrite.dstBinding = 3;
	indirectWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	indirectWrite.descriptorCount = 1;
	indirectWrite.pBufferInfo = &indirectBufferInfo;

	// 修改：提交 3 个写入操作
	VkWriteDescriptorSet writes[] = { matWrite, instWrite, indirectWrite };
	vkUpdateDescriptorSets(_vkContext.device, 3, writes, 0, nullptr);
}

AllocatedImage AeroEngine::upload_texture(void* pixels, int width, int height, VkFormat format) {
	uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
	VkExtent3D imageExtent = { (uint32_t)width, (uint32_t)height, 1 };
	VkDeviceSize imageSize = width * height * 4;

	VkImageCreateInfo dimg_info{};
	dimg_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	dimg_info.imageType = VK_IMAGE_TYPE_2D;
	dimg_info.format = format;
	dimg_info.extent = imageExtent;
	dimg_info.mipLevels = mipLevels;
	dimg_info.arrayLayers = 1;
	dimg_info.samples = VK_SAMPLE_COUNT_1_BIT;
	dimg_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	dimg_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo dimg_allocinfo{};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	AllocatedImage newImage;
	newImage.imageFormat = format;
	newImage.imageExtent = imageExtent;
	VK_CHECK(vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo,
		&newImage.image, &newImage.allocation, nullptr));

	VkBufferCreateInfo stagingInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	stagingInfo.size = imageSize;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo stagingAllocInfo = {};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
	stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	AllocatedBuffer stagingBuffer;
	VmaAllocationInfo stagingAllocResult;
	VK_CHECK(vmaCreateBuffer(_allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, &stagingAllocResult));

	memcpy(stagingAllocResult.pMappedData, pixels, static_cast<size_t>(imageSize));

	// 3. 异步提交：Transition Layout (Undefined -> TransferDst) -> Copy Buffer To Image -> Transition Layout (TransferDst -> ShaderReadOnly)
	immediate_submit([&](VkCommandBuffer cmd) {
		vkinit::transition_image_mip(cmd, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, mipLevels);

		VkBufferImageCopy copyRegion = {};
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = imageExtent;
		vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		int32_t mipWidth = width;
		int32_t mipHeight = height;

		for (uint32_t i = 1; i < mipLevels; i++) {
			vkinit::transition_image_mip(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, i - 1, 1);

			VkImageBlit blit{};
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.layerCount = 1;

			blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.layerCount = 1;

			vkCmdBlitImage(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

			vkinit::transition_image_mip(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, i - 1, 1);

			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}

		vkinit::transition_image_mip(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels - 1, 1);
		});

	vmaDestroyBuffer(_allocator, stagingBuffer.buffer, stagingBuffer.allocation);

	VkImageViewCreateInfo view_info{};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = newImage.image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = mipLevels;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;

	VK_CHECK(vkCreateImageView(_vkContext.device, &view_info, nullptr, &newImage.view));

	return newImage;
}

void AeroEngine::update_bindless_texture(const AllocatedImage& image, uint32_t textureID) {
	VkDescriptorImageInfo imageBufferInfo{};
	imageBufferInfo.sampler = _defaultSamplerLinear;
	imageBufferInfo.imageView = image.view;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet textureWrite{};
	textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	textureWrite.dstSet = _globalDescriptorSet;
	textureWrite.dstBinding = 1; // 绑在 Binding 1 (globalTextures)
	textureWrite.dstArrayElement = textureID; // 关键！插到数组的哪个索引
	textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textureWrite.descriptorCount = 1;
	textureWrite.pImageInfo = &imageBufferInfo;

	vkUpdateDescriptorSets(_vkContext.device, 1, &textureWrite, 0, nullptr);
}

void AeroEngine::upload_scene_data(const SceneData& scene) {
	std::cout << "[AeroEngine] Starting scene upload to GPU..." << std::endl;

	uint32_t whitePixel = 0xFFFFFFFF; // RGBA 全部为 255
	AllocatedImage defaultTexture = upload_texture(&whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM);
	_sceneTextures.push_back(defaultTexture); // 交给现有的资源数组统一管理销毁

	// 将 1024 个槽位全部初始化为安全的安全贴图
	for (uint32_t i = 0; i < 4096; ++i) {
		update_bindless_texture(defaultTexture, i);
	}

	_mainMeshBuffers = upload_mesh_data(scene);
	_renderables = scene.subMeshes;

	_materialBuffer = upload_ssbo_data(scene.materials.size() * sizeof(MaterialParams), scene.materials.data());

	// 更新全局 Descriptor Set 的 Binding 0 (材质 SSBO)
	_instanceCount = static_cast<uint32_t>(scene.subMeshes.size());
	std::vector<InstanceData> instances(_instanceCount);
	for (size_t i = 0; i < _instanceCount; ++i) {
		const SubMesh& mesh = scene.subMeshes[i];
		instances[i].modelMatrix = glm::mat4(1.0f); // 当前 glTF 解析器还未提取节点 Transform，先填单位阵

		//利用 float 存储 int，在 Shader 里用 floatBitsToUint 转回来，保证 vec4 对齐
		float matIDAsFloat;
		uint32_t matID = mesh.materialIndex;
		memcpy(&matIDAsFloat, &matID, sizeof(float));

		instances[i].aabbMin_MatID = glm::vec4(mesh.aabbMin, matIDAsFloat);
		instances[i].aabbMax_Pad = glm::vec4(mesh.aabbMax, 0.0f);
		instances[i].indexCount = mesh.indexCount;
		instances[i].firstIndex = mesh.firstIndex;
		instances[i].vertexOffset = mesh.vertexOffset;
	}

	_instanceBuffer = upload_ssbo_data(instances.size() * sizeof(InstanceData), instances.data());

	VkBufferCreateInfo indirectInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	indirectInfo.size = _instanceCount * sizeof(VkDrawIndexedIndirectCommand);
	indirectInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo indirectAllocInfo = {};
	indirectAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateBuffer(_allocator, &indirectInfo, &indirectAllocInfo,
		&_drawIndirectBuffer.buffer, &_drawIndirectBuffer.allocation, nullptr));

	// 更新全局 Descriptor Set 的 Binding 0 (材质) 和 Binding 2 (Instance)
	update_global_descriptor_set();

	//遍历图片并上传到 GPU 的 Bindless 数组
	int loadedTextureCount = 0;
	for (size_t i = 0; i < scene.images.size(); i++) {
		const LoadedImage& img = scene.images[i];

		if (img.pixels != nullptr) {
			AllocatedImage gpuImage = upload_texture(img.pixels, img.width, img.height, VK_FORMAT_R8G8B8A8_UNORM);

			update_bindless_texture(gpuImage, static_cast<uint32_t>(i));

			_sceneTextures.push_back(gpuImage);

			stbi_image_free(img.pixels);
			loadedTextureCount++;
		}
	}

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, _drawIndirectBuffer.buffer, _drawIndirectBuffer.allocation);
		vmaDestroyBuffer(_allocator, _mainMeshBuffers.vertexBuffer.buffer, _mainMeshBuffers.vertexBuffer.allocation);
		vmaDestroyBuffer(_allocator, _mainMeshBuffers.indexBuffer.buffer, _mainMeshBuffers.indexBuffer.allocation);
		vmaDestroyBuffer(_allocator, _materialBuffer.buffer, _materialBuffer.allocation);
		vmaDestroyBuffer(_allocator, _instanceBuffer.buffer, _instanceBuffer.allocation);
		for (const AllocatedImage& img : _sceneTextures) {
			vkDestroyImageView(_vkContext.device, img.view, nullptr);
			vmaDestroyImage(_allocator, img.image, img.allocation);
		}
		});

	std::cout << "[AeroEngine] Successfully uploaded scene to GPU! (Textures loaded: " << loadedTextureCount << ")" << std::endl;
}

////to test gpu culling, I duplicated 10 sponza scenes
//void AeroEngine::upload_scene_data(const SceneData& scene) {
//	std::cout << "[AeroEngine] Starting scene upload to GPU..." << std::endl;
//
//	uint32_t whitePixel = 0xFFFFFFFF; // RGBA 全部为 255
//	AllocatedImage defaultTexture = upload_texture(&whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM);
//	_sceneTextures.push_back(defaultTexture); // 交给现有的资源数组统一管理销毁
//
//	for (uint32_t i = 0; i < 4096; ++i) {
//		update_bindless_texture(defaultTexture, i);
//	}
//
//	_mainMeshBuffers = upload_mesh_data(scene);
//	_renderables = scene.subMeshes;
//
//	_materialBuffer = upload_ssbo_data(scene.materials.size() * sizeof(MaterialParams), scene.materials.data());
//
//	// 更新全局 Descriptor Set 的 Binding 0 (材质 SSBO)
//	_instanceCount = static_cast<uint32_t>(scene.subMeshes.size())*100;
//	std::vector<InstanceData> instances(_instanceCount);
//
//	int gridDim = 10;
//	float spacing = 5000.0f; // 每个 Sponza 之间的间距
//
//	for (int x = 0; x < gridDim; x++) {
//		for (int z = 0; z < gridDim; z++) {
//			for (size_t i = 0; i < scene.subMeshes.size(); i++) {
//				int index = (x * gridDim + z) * scene.subMeshes.size() + i;
//				const SubMesh& mesh = scene.subMeshes[i];
//
//				glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x * spacing, 0.0f, z * spacing));
//				instances[index].modelMatrix = transform;
//
//				float matIDAsFloat;
//				uint32_t matID = mesh.materialIndex;
//				memcpy(&matIDAsFloat, &matID, sizeof(float));
//
//				instances[index].aabbMin_MatID = glm::vec4(mesh.aabbMin, matIDAsFloat);
//				instances[index].aabbMax_Pad = glm::vec4(mesh.aabbMax, 0.0f);
//				instances[index].indexCount = mesh.indexCount;
//				instances[index].firstIndex = mesh.firstIndex;
//				instances[index].vertexOffset = mesh.vertexOffset;
//			}
//		}
//	}
//
//	_instanceBuffer = upload_ssbo_data(instances.size() * sizeof(InstanceData), instances.data());
//
//	VkBufferCreateInfo indirectInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
//	indirectInfo.size = _instanceCount * sizeof(VkDrawIndexedIndirectCommand);
//	indirectInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
//
//	VmaAllocationCreateInfo indirectAllocInfo = {};
//	indirectAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
//
//	VK_CHECK(vmaCreateBuffer(_allocator, &indirectInfo, &indirectAllocInfo,
//		&_drawIndirectBuffer.buffer, &_drawIndirectBuffer.allocation, nullptr));
//
//	update_global_descriptor_set();
//
//	int loadedTextureCount = 0;
//	for (size_t i = 0; i < scene.images.size(); i++) {
//		const LoadedImage& img = scene.images[i];
//
//		if (img.pixels != nullptr) {
//			AllocatedImage gpuImage = upload_texture(img.pixels, img.width, img.height, VK_FORMAT_R8G8B8A8_UNORM);
//
//			update_bindless_texture(gpuImage, static_cast<uint32_t>(i));
//
//			_sceneTextures.push_back(gpuImage);
//
//			stbi_image_free(img.pixels);
//			loadedTextureCount++;
//		}
//	}
//
//	_mainDeletionQueue.push_function([=]() {
//		vmaDestroyBuffer(_allocator, _drawIndirectBuffer.buffer, _drawIndirectBuffer.allocation);
//		vmaDestroyBuffer(_allocator, _mainMeshBuffers.vertexBuffer.buffer, _mainMeshBuffers.vertexBuffer.allocation);
//		vmaDestroyBuffer(_allocator, _mainMeshBuffers.indexBuffer.buffer, _mainMeshBuffers.indexBuffer.allocation);
//		vmaDestroyBuffer(_allocator, _materialBuffer.buffer, _materialBuffer.allocation);
//		vmaDestroyBuffer(_allocator, _instanceBuffer.buffer, _instanceBuffer.allocation);
//		for (const AllocatedImage& img : _sceneTextures) {
//			vkDestroyImageView(_vkContext.device, img.view, nullptr);
//			vmaDestroyImage(_allocator, img.image, img.allocation);
//		}
//		});
//
//	std::cout << "[AeroEngine] Successfully uploaded scene to GPU! (Textures loaded: " << loadedTextureCount << ")" << std::endl;
//}