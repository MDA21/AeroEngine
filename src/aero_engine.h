#pragma once
#include "vk_types.h";
#include "vk_context.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

struct GLFWwindow;

struct FrameData {
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkSemaphore _swapchainSemaphore; // 图像准备就绪信号 (GPU -> GPU)
	VkFence _renderFence;            // 帧执行完成围栏 (GPU -> CPU)
};

class AeroEngine {
public:
	static AeroEngine& Get();

	void init();
	void cleanup();
	void run();

private:
	void init_window();
	void init_vulkan();
	void init_swapchain();
	void init_allocator();
	void init_commands();
	void init_sync_structures();
	void init_pipelines();

	void draw();
	
	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; }

	bool _isInitialized{ false };
	bool _stopRendering{ false };

	VkExtent2D _windowExtent{ 1280, 720 };
	GLFWwindow* _window{ nullptr };

	DeletionQueue _mainDeletionQueue;

	//Vulkan hardware context
	VulkanContext _vkContext;

	//swapchain 
	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	std::vector<VkSemaphore> _renderSemaphores;

	VmaAllocator _allocator;

	//frames
	FrameData _frames[FRAME_OVERLAP];
	uint32_t _frameNumber{ 0 };
	
	//pipelines
	VkPipelineLayout _trianglePipelineLayout;
	VkPipeline _trianglePipeline;
};