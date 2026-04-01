#include "aero_engine.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <VkBootstrap.h>
#include "vk_initializers.h"
#include <vk_pipelines.h>

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

	init_pipelines();

	init_imgui();

	_isInitialized = true;
	
	std::cout << "[AeroEngine] Initialization complete." << std::endl;
}

void AeroEngine::run() {
	while (!glfwWindowShouldClose(_window) && !_stopRendering) {
		glfwPollEvents();
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
}

void AeroEngine::init_swapchain() {
	vkb::SwapchainBuilder swapchainBuilder{ _vkContext.chosenGPU, _vkContext.device, _vkContext.surface };

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
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
	if (!vkutil::load_shader_module("shaders/colored_triangle.frag.spv", _vkContext.device, &triangleFragShader)) {
		std::cout << "Error when building the triangle fragment shader module" << std::endl;
	}
	VkShaderModule triangleVertShader;
	if (!vkutil::load_shader_module("shaders/colored_triangle.vert.spv", _vkContext.device, &triangleVertShader)) {
		std::cout << "Error when building the triangle vertex shader module" << std::endl;
	}

	//创建 Pipeline Layout (目前为空，未来用于传 Push Constants 或 Descriptor Sets)
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
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
	VkRenderingInfo renderInfo = vkinit::rendering_info(_windowExtent, &colorAttachment, nullptr);

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

	vkCmdDraw(cmd, 3, 1, 0, 0);

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