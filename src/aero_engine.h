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
	void init_bindless_descriptor();
	void init_depth_image();

	void draw();

	void init_imgui();

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	GPUMeshBuffers upload_mesh_data(const SceneData& scene);

	AllocatedBuffer upload_material_data(const std::vector<MaterialParams>& materials);

	void upload_scene_data(const SceneData& scene);

	void update_global_descriptor_set(VkBuffer materialBuffer, size_t bufferSize);

	AllocatedImage upload_texture(void* pixels, int width, int height, VkFormat format);

	void update_bindless_texture(const AllocatedImage& image, uint32_t textureID);

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

	//imgui
	VkDescriptorPool _imguiPool;

	//bindless descriptor
	VkDescriptorPool _globalDescriptorPool;
	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSet _globalDescriptorSet;

	//sampler
	VkSampler _defaultSamplerLinear;

	//mesh buffer
	GPUMeshBuffers _mainMeshBuffers;

	//material SSBO
	AllocatedBuffer _materialBuffer;

	std::vector<AllocatedImage> _sceneTextures;

	std::vector<SubMesh> _renderables;

	//depthImage
	AllocatedImage _depthImage;
	VkFormat _depthImageFormat{ VK_FORMAT_D32_SFLOAT };
};