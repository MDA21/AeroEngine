#include "aero_engine.h"
#include <GLFW/glfw3.h>
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

struct MeshPushConstants {
	glm::mat4 render_matrix;
	uint32_t material_id;
};

AeroEngine& AeroEngine::Get() {
	static AeroEngine engine;
	return engine;
}

void AeroEngine::init() {
	init_window();

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
	_lastFrameTime = static_cast<float>(glfwGetTime());

	while (!glfwWindowShouldClose(_window) && !_stopRendering) {
		float currentFrameTime = static_cast<float>(glfwGetTime());
		_deltaTime = currentFrameTime - _lastFrameTime;
		_lastFrameTime = currentFrameTime;

		glfwPollEvents();

		process_input();

		draw();
	}
}

void AeroEngine::cleanup() {
	if (_isInitialized) {
		vkDeviceWaitIdle(_vkContext.device);
		_mainDeletionQueue.flush();
		_isInitialized = false;
	}
}

void AeroEngine::process_input() {
	// 1. 处理鼠标 (右键按住时才控制相机)
	if (glfwGetMouseButton(_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
		// 隐藏并锁定光标
		glfwSetInputMode(_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

		double xpos, ypos;
		glfwGetCursorPos(_window, &xpos, &ypos);

		if (_firstMouse) {
			_lastMouseX = xpos;
			_lastMouseY = ypos;
			_firstMouse = false;
		}

		float xoffset = static_cast<float>(xpos - _lastMouseX);
		float yoffset = static_cast<float>(ypos - _lastMouseY);

		_lastMouseX = xpos;
		_lastMouseY = ypos;

		_camera.ProcessMouseMovement(xoffset, yoffset);
	}
	else {
		// 松开右键，恢复光标显示，允许操作 ImGui
		glfwSetInputMode(_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		_firstMouse = true; // 重置标志位，防止下次右键时镜头瞬移
	}

	// 2. 处理键盘 (WASD + Space + Ctrl + Shift)
	bool isSprint = glfwGetKey(_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

	if (glfwGetKey(_window, GLFW_KEY_W) == GLFW_PRESS)
		_camera.ProcessKeyboard(CameraMovement::FORWARD, _deltaTime, isSprint);
	if (glfwGetKey(_window, GLFW_KEY_S) == GLFW_PRESS)
		_camera.ProcessKeyboard(CameraMovement::BACKWARD, _deltaTime, isSprint);
	if (glfwGetKey(_window, GLFW_KEY_A) == GLFW_PRESS)
		_camera.ProcessKeyboard(CameraMovement::LEFT, _deltaTime, isSprint);
	if (glfwGetKey(_window, GLFW_KEY_D) == GLFW_PRESS)
		_camera.ProcessKeyboard(CameraMovement::RIGHT, _deltaTime, isSprint);
	if (glfwGetKey(_window, GLFW_KEY_SPACE) == GLFW_PRESS)
		_camera.ProcessKeyboard(CameraMovement::UP, _deltaTime, isSprint);
	if (glfwGetKey(_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
		_camera.ProcessKeyboard(CameraMovement::DOWN, _deltaTime, isSprint);
}

void AeroEngine::init_window() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	_window = glfwCreateWindow(_windowExtent.width, _windowExtent.height, "AeroEngine", nullptr, nullptr);

	_mainDeletionQueue.push_function([this]() {
		glfwDestroyWindow(_window);
		glfwTerminate();
		std::cout << "[AeroEngine] Window destroyed." << std::endl;
		});
}

void AeroEngine::init_vulkan() {
	_vkContext.init(_window, _mainDeletionQueue);

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
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
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
	pushConstant.size = sizeof(glm::mat4) + sizeof(uint32_t); // 矩阵 + 材质ID (未来可扩展)
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

	//拓扑结构：三角形列表
	builder._inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	builder._inputAssembly.primitiveRestartEnable = VK_FALSE;

	// 获取顶点描述
	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	// 配置顶点输入状态
	builder._vertexInputInfo = {};
	builder._vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	builder._vertexInputInfo.vertexBindingDescriptionCount = 1;
	builder._vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	builder._vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
	builder._vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	builder._depthStencil.depthTestEnable = VK_TRUE;
	builder._depthStencil.depthWriteEnable = VK_TRUE;
	builder._depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;

	//光栅化设置：实心填充，不剔除（因为我们随便画的，防止看不见）
	builder._rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	builder._rasterizer.lineWidth = 1.0f;
	builder._rasterizer.cullMode = VK_CULL_MODE_NONE;
	builder._rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

	//多重采样：默认关闭
	builder._multisampling.sampleShadingEnable = VK_FALSE;
	builder._multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	//颜色混合：覆盖模式
	builder._colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	builder._colorBlendAttachment.blendEnable = VK_FALSE;

	// 1.3 动态渲染配置：告诉管线我们将画到什么格式的图片上
	builder._renderInfo.colorAttachmentCount = 1;
	builder._renderInfo.pColorAttachmentFormats = &_swapchainImageFormat;
	builder._renderInfo.depthAttachmentFormat = _depthImageFormat;

	//构建管线
	_trianglePipeline = builder.build_pipeline(_vkContext.device);

	// 4. 清理着色器模块 (编译成管线后，Shader Module 就可以销毁了)
	vkDestroyShaderModule(_vkContext.device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, triangleVertShader, nullptr);

	//压入全局销毁队列
	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(_vkContext.device, _trianglePipeline, nullptr);
		vkDestroyPipelineLayout(_vkContext.device, _trianglePipelineLayout, nullptr);
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
	ImGui_ImplGlfw_InitForVulkan(_window, true);

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

	static float fpsTimer = 0.0f;
	static float displayFps = 0.0f;
	static float displayMs = 0.0f;

	fpsTimer += ImGui::GetIO().DeltaTime;
	if (fpsTimer > 0.5f) {
		displayFps = ImGui::GetIO().Framerate;
		displayMs = 1000.0f / displayFps;
		fpsTimer = 0.0f;
	}

	ImGui::Text("Performance: %.3f ms/frame (%.1f FPS)", displayMs, displayFps);

	ImGui::Separator();
	ImGui::Text("VSync: %s", (_swapchainImageFormat == VK_PRESENT_MODE_FIFO_KHR) ? "ON (FIFO)" : "OFF (MAILBOX)");

	ImGui::Separator();
	ImGui::Text("Camera Settings");
	ImGui::SliderFloat("Speed", &_camera.MovementSpeed, 1.0f, 50.0f);
	ImGui::SliderFloat("Sensitivity", &_camera.MouseSensitivity, 0.01f, 1.0f);
	ImGui::Text("Position: (%.1f, %.1f, %.1f)", _camera.Position.x, _camera.Position.y, _camera.Position.z);
	ImGui::Text("Hold Right-Click to look around.");

	ImGui::End();

	ImGui::Render();

	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_vkContext.device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex));

	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));


	vkinit::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkClearValue clearValue;
	clearValue.color = { {0.05f, 0.05f, 0.08f, 1.0f} };

	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_swapchainImageViews[swapchainImageIndex], &clearValue);
	VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_depthImage.view);

	VkRenderingInfo renderInfo = vkinit::rendering_info(_windowExtent, &colorAttachment, &depthAttachment);

	vkCmdBeginRendering(cmd, &renderInfo);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)_windowExtent.width;
	viewport.height = (float)_windowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = _windowExtent;
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);

	if (!_renderables.empty() && _mainMeshBuffers.vertexBuffer.buffer != VK_NULL_HANDLE) {
		// 绑定全局 Bindless 描述符集
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipelineLayout, 0, 1, &_globalDescriptorSet, 0, nullptr);

		// 绑定顶点和索引 Buffer
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &_mainMeshBuffers.vertexBuffer.buffer, &offset);
		vkCmdBindIndexBuffer(cmd, _mainMeshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		glm::mat4 view = _camera.GetViewMatrix();
		glm::mat4 proj = glm::perspectiveZO(glm::radians(_camera.Fov), (float)_windowExtent.width / (float)_windowExtent.height, 10000.0f, 0.1f);//reverse Z
		proj[1][1] *= -1; // Vulkan Y轴翻转
		glm::mat4 viewProj = proj * view;

		for (const SubMesh& submesh : _renderables) {
			MeshPushConstants pushData;
			pushData.render_matrix = viewProj;
			pushData.material_id = submesh.materialIndex;

			vkCmdPushConstants(cmd, _trianglePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstants), &pushData);
			vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.firstIndex, submesh.vertexOffset, 0);
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

	VK_CHECK(vkQueuePresentKHR(_vkContext.graphicsQueue, &presentInfo));

	_frameNumber++;
}

void AeroEngine::init_depth_image() {
	VkExtent3D depthImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
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

	// 计算总大小
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
	textureBind.descriptorCount = 1024;
	textureBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding bindings[]{ materialBind, textureBind };

	VkDescriptorBindingFlags bindlessFlags =
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

	VkDescriptorBindingFlags bindingFlags[2] = { 0, bindlessFlags };

	VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
	extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	extendedInfo.bindingCount = 2;
	extendedInfo.pBindingFlags = bindingFlags;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = &extendedInfo;
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	layoutInfo.bindingCount = 2;
	layoutInfo.pBindings = bindings;

	VK_CHECK(vkCreateDescriptorSetLayout(_vkContext.device, &layoutInfo, nullptr, &_globalSetLayout));

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 }
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

AllocatedBuffer AeroEngine::upload_material_data(const std::vector<MaterialParams>& materials) {
	const size_t bufferSize = materials.size() * sizeof(MaterialParams);

	VkBufferCreateInfo ssboInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	ssboInfo.size = bufferSize;
	ssboInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	AllocatedBuffer materialBuffer;
	VK_CHECK(vmaCreateBuffer(_allocator, &ssboInfo, &vmaAllocInfo,
		&materialBuffer.buffer, &materialBuffer.allocation, nullptr));

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

	memcpy(stagingAllocResult.pMappedData, materials.data(), bufferSize);

	immediate_submit([&](VkCommandBuffer cmd) {
		VkBufferCopy copyRegion = { 0, 0, bufferSize };
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, materialBuffer.buffer, 1, &copyRegion);
		});

	vmaDestroyBuffer(_allocator, stagingBuffer.buffer, stagingBuffer.allocation);

	return materialBuffer;
}

void AeroEngine::update_global_descriptor_set(VkBuffer materialBuffer, size_t bufferSize) {
	VkDescriptorBufferInfo bufferInfo{};
	bufferInfo.buffer = materialBuffer;
	bufferInfo.offset = 0;
	bufferInfo.range = bufferSize;

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = _globalDescriptorSet;
	write.dstBinding = 0; // Binding 0 是我们配置的 Material SSBO
	write.dstArrayElement = 0;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write.descriptorCount = 1;
	write.pBufferInfo = &bufferInfo;

	vkUpdateDescriptorSets(_vkContext.device, 1, &write, 0, nullptr);
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
	for (uint32_t i = 0; i < 1024; ++i) {
		update_bindless_texture(defaultTexture, i);
	}

	// 1. 上传顶点和索引 Buffer，并存入成员变量供 draw() 使用
	_mainMeshBuffers = upload_mesh_data(scene);
	_renderables = scene.subMeshes;

	// 2. 上传材质大数组到 SSBO
	_materialBuffer = upload_material_data(scene.materials);

	// 更新全局 Descriptor Set 的 Binding 0 (材质 SSBO)
	update_global_descriptor_set(_materialBuffer.buffer, scene.materials.size() * sizeof(MaterialParams));

	// 3. 遍历图片并上传到 GPU 的 Bindless 数组
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
		vmaDestroyBuffer(_allocator, _mainMeshBuffers.vertexBuffer.buffer, _mainMeshBuffers.vertexBuffer.allocation);
		vmaDestroyBuffer(_allocator, _mainMeshBuffers.indexBuffer.buffer, _mainMeshBuffers.indexBuffer.allocation);
		vmaDestroyBuffer(_allocator, _materialBuffer.buffer, _materialBuffer.allocation);
		for (const AllocatedImage& img : _sceneTextures) {
			vkDestroyImageView(_vkContext.device, img.view, nullptr);
			vmaDestroyImage(_allocator, img.image, img.allocation);
		}
		});

	std::cout << "[AeroEngine] Successfully uploaded scene to GPU! (Textures loaded: " << loadedTextureCount << ")" << std::endl;
}