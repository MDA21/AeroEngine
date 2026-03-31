#pragma once
#include "vk_types.h"

struct GLFWwindow;

class VulkanContext
{
public:
	void init(GLFWwindow* window, DeletionQueue& deletionQueue);

	VkInstance instance{ VK_NULL_HANDLE };
	VkDebugUtilsMessengerEXT debugMessenger { VK_NULL_HANDLE };
	VkSurfaceKHR surface{ VK_NULL_HANDLE };
	VkPhysicalDevice chosenGPU{ VK_NULL_HANDLE };
	VkDevice device{ VK_NULL_HANDLE };

	VkQueue graphicsQueue{ VK_NULL_HANDLE };
	uint32_t graphicsQueueFamily{ 0 };
};
