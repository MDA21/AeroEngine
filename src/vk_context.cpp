#include "vk_context.h"
#include <VkBootstrap.h>
#include <GLFW/glfw3.h>
#include <iostream>

void VulkanContext::init(GLFWwindow* window, DeletionQueue& deletionQueue) {
	VK_CHECK(volkInitialize());

    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("AeroEngine")
        .request_validation_layers(true)
        .require_api_version(1, 3, 0)
        .use_default_debug_messenger()
        .build();

    if (!inst_ret) {
        std::cerr << "Failed to create Vulkan instance: " << inst_ret.error().message() << "\n";
        abort();
    }
    vkb::Instance vkb_inst = inst_ret.value();
    instance = vkb_inst.instance;
    debugMessenger = vkb_inst.debug_messenger;

    volkLoadInstance(instance);

    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE; 
    features12.descriptorIndexing = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    auto phys_ret = selector.set_minimum_version(1, 3)
        .set_surface(surface)
        .select();

    if (!phys_ret) {
        std::cerr << "Failed to find a suitable GPU: " << phys_ret.error().message() << "\n";
        abort();
    }

    vkb::PhysicalDevice physicalDevice = phys_ret.value();
    chosenGPU = physicalDevice.physical_device;
    std::cout << "[VulkanContext] Selected GPU: " << physicalDevice.name << std::endl;

    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    auto dev_ret = deviceBuilder.add_pNext(&features13)
        .add_pNext(&features12)
        .build();
    if (!dev_ret) {
        std::cerr << "Failed to create Vulkan logical device: " << dev_ret.error().message() << "\n";
        abort();
    }

    vkb::Device vkbDevice = dev_ret.value();
    device = vkbDevice.device;
    volkLoadDevice(device);

    graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    deletionQueue.push_function([this]() {
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkb::destroy_debug_utils_messenger(instance, debugMessenger);
        vkDestroyInstance(instance, nullptr);
        std::cout << "[VulkanContext] Hardware context destroyed." << std::endl;
        });
}