#pragma once
#include "camera.h"
#include "Core/Window.h"
#include "RHI/VulkanDevice.h"
#include "RHI/vk_types.h"
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
	void recreate_swapchain();
	void create_render_semaphores();
	void destroy_render_semaphores();

	void init_imgui();

	void process_input();

	struct PerformanceStats {
		float displayMs{ 0.0f };
		float displayFps{ 0.0f };
		float cpuFrameMs{ 0.0f };
		float gpuFrameMs{ 0.0f };
		float cullingGpuMs{ 0.0f };
		float uploadCpuMs{ 0.0f };
		uint64_t uploadBytes{ 0 };
	};
	static_assert(sizeof(PerformanceStats) == 32, "PerformanceStats should stay compact");

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
	PerformanceStats _perfStats;
	PerformanceStats _displayPerfStats;


};
