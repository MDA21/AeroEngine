#pragma once
#include "vk_types.h";
//#include "vk_context.h"
#include "camera.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "Core/Window.h"
#include "RHI/VulkanDevice.h"

struct GLFWwindow;


class AeroEngine {
public:
	static AeroEngine& Get();

	void init();
	void cleanup();
	void run();

private:
	void init_default_sampler();
	void init_pipelines();
	void init_bindless_descriptor();
	void init_depth_image();

	void draw();

	void init_imgui();

	GPUMeshBuffers upload_mesh_data(const SceneData& scene);

	AllocatedBuffer upload_ssbo_data(size_t bufferSize, const void* data);

	void upload_scene_data(const SceneData& scene);

	//for test
	void upload_scene_data_duplicate (const SceneData& scene);

	void update_global_descriptor_set();

	AllocatedImage upload_texture(void* pixels, int width, int height, VkFormat format);

	void update_bindless_texture(const AllocatedImage& image, uint32_t textureID);


	void process_input();

	bool _isInitialized{ false };
	bool _stopRendering{ false };
	bool _useGPUDriven{ true };

	std::unique_ptr<Aero::Window> _window;
	std::unique_ptr<Aero::RHI::VulkanDevice> _renderDevice;

	DeletionQueue _mainDeletionQueue;

	std::vector<VkSemaphore> _renderSemaphores;
	
	//pipelines
	VkPipelineLayout _trianglePipelineLayout;
	VkPipeline _trianglePipeline;
	VkPipelineLayout _cullingPipelineLayout;
	VkPipeline _cullingPipeline;

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

	//
	AllocatedBuffer _drawIndirectBuffer;

	std::vector<AllocatedImage> _sceneTextures;

	std::vector<SubMesh> _renderables;

	//depthImage
	AllocatedImage _depthImage;
	VkFormat _depthImageFormat{ VK_FORMAT_D32_SFLOAT };

	//camera
	Camera _camera{ glm::vec3(0.0f, 1.0f, 5.0f) };

	//data for gpu driven
	//instance SSBO
	AllocatedBuffer _instanceBuffer;
	uint32_t _instanceCount{ 0 };

};