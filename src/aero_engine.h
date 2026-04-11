#pragma once
#include "vk_types.h";
#include "camera.h"
#include "Core/Window.h"
#include "RHI/VulkanDevice.h"
#include "Renderer/SceneRenderer.h"

struct GLFWwindow;


class AeroEngine {
public:
	static AeroEngine& Get();

	void init();
	void cleanup();
	void run();

private:
	void draw();

	void init_imgui();

	void process_input();

	bool _isInitialized{ false };
	bool _useGPUDriven{ true };

	std::unique_ptr<Aero::Window> _window;
	std::unique_ptr<Aero::RHI::VulkanDevice> _renderDevice;
	std::unique_ptr<Aero::Renderer::SceneRenderer> _sceneRenderer;

	DeletionQueue _mainDeletionQueue;

	std::vector<VkSemaphore> _renderSemaphores;
	
	//imgui
	VkDescriptorPool _imguiPool;

	Camera _camera{ glm::vec3(0.0f, 1.0f, 5.0f) };


};