#include "VulkanDevice.h"
#include <VkBootstrap.h>
#include <iostream>

#define	GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Aero {
	namespace RHI {
		void VulkanDevice::init(Aero::Window* window, DeletionQueue& deletionQueue) {
			_deletionQueue = &deletionQueue;

			init_vulkan(window);
			init_swapchain(window);
			init_allocator();
			init_commands();
			init_sync_structures();

			std::cout << "[RHI] Vulkan Device successfully initialized." << std::endl;
		}

		FrameData& VulkanDevice::get_current_frame() {
			return _frames[_frameNumber % FRAME_OVERLAP];
		}

		void VulkanDevice::init_vulkan(Aero::Window* window) {
			VK_CHECK(volkInitialize());

			vkb::InstanceBuilder builder;
			auto inst_ret = builder.set_app_name("AeroEngine")
				.request_validation_layers(true)
				.require_api_version(1, 3, 0)
				.use_default_debug_messenger()
				.build();

			vkb::Instance vkb_inst = inst_ret.value();
			_instance = vkb_inst.instance;
			_debugMessenger = vkb_inst.debug_messenger;
			volkLoadInstance(_instance);

			VK_CHECK(glfwCreateWindowSurface(_instance, window->handle(), nullptr, &_surface));

			VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			features13.dynamicRendering = VK_TRUE;
			features13.synchronization2 = VK_TRUE;

			VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			features12.bufferDeviceAddress = VK_TRUE;
			features12.descriptorIndexing = VK_TRUE;
			features12.descriptorBindingPartiallyBound = VK_TRUE;
			features12.runtimeDescriptorArray = VK_TRUE;
			features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
			features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

			VkPhysicalDeviceFeatures baseFeatures{};
			baseFeatures.multiDrawIndirect = VK_TRUE;

			vkb::PhysicalDeviceSelector selector{ vkb_inst };
			vkb::PhysicalDevice physicalDevice = selector
				.set_minimum_version(1, 3)
				.set_surface(_surface)
				.set_required_features(baseFeatures)
				.select()
				.value();

			_chosenGPU = physicalDevice.physical_device;

			vkb::DeviceBuilder deviceBuilder{ physicalDevice };
			vkb::Device vkbDevice = deviceBuilder.add_pNext(&features13).add_pNext(&features12).build().value();

			_device = vkbDevice.device;
			volkLoadDevice(_device);

			_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
			_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

			_transferQueue = vkbDevice.get_queue(vkb::QueueType::transfer).value();
			_transferQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::transfer).value();

			_deletionQueue->push_function([=]() {
				vkDestroyDevice(_device, nullptr);
				vkDestroySurfaceKHR(_instance, _surface, nullptr);
				vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
				vkDestroyInstance(_instance, nullptr);
				});
		}

		void VulkanDevice::init_swapchain(Aero::Window* window) {
			vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };
			vkb::Swapchain vkbSwapchain = swapchainBuilder
				.use_default_format_selection()
				.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
				.set_desired_extent(window->width(), window->height())
				.build()
				.value();

			_swapchain = vkbSwapchain.swapchain;
			_swapchainImageFormat = vkbSwapchain.image_format;
			_swapchainImages = vkbSwapchain.get_images().value();
			_swapchainImageViews = vkbSwapchain.get_image_views().value();

			_deletionQueue->push_function([=]() {
				for (VkImageView view : _swapchainImageViews) vkDestroyImageView(_device, view, nullptr);
				vkDestroySwapchainKHR(_device, _swapchain, nullptr);
				});
		}

		void VulkanDevice::init_allocator() {
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
			allocatorInfo.physicalDevice = _chosenGPU;
			allocatorInfo.device = _device;
			allocatorInfo.instance = _instance;
			allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
			allocatorInfo.pVulkanFunctions = &vulkanFunctions;

			VK_CHECK(vmaCreateAllocator(&allocatorInfo, &_allocator));

			_deletionQueue->push_function([=]() {
				vmaDestroyAllocator(_allocator);
				});
		}

		void VulkanDevice::init_commands() {
			VkCommandPoolCreateInfo commandPoolInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			commandPoolInfo.queueFamilyIndex = _graphicsQueueFamily;

			for (int i = 0; i < FRAME_OVERLAP; i++) {
				VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i].commandPool));

				VkCommandBufferAllocateInfo cmdAllocInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
				cmdAllocInfo.commandPool = _frames[i].commandPool;
				cmdAllocInfo.commandBufferCount = 1;
				cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

				VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].mainCommandBuffer));

				_deletionQueue->push_function([=]() {
					vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);
					});
			}

			VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_uploadContext.commandPool));
			VkCommandBufferAllocateInfo uploadAlloc{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			uploadAlloc.commandPool = _uploadContext.commandPool;
			uploadAlloc.commandBufferCount = 1;
			uploadAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			VK_CHECK(vkAllocateCommandBuffers(_device, &uploadAlloc, &_uploadContext.commandBuffer));

			VkFenceCreateInfo fenceInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			VK_CHECK(vkCreateFence(_device, &fenceInfo, nullptr, &_uploadContext.uploadFence));

			_deletionQueue->push_function([=]() {
				vkDestroyFence(_device, _uploadContext.uploadFence, nullptr);
				vkDestroyCommandPool(_device, _uploadContext.commandPool, nullptr);
				});
		}

		void VulkanDevice::init_sync_structures() {
			VkFenceCreateInfo fenceCreateInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			VkSemaphoreCreateInfo semaphoreCreateInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

			for (int i = 0; i < FRAME_OVERLAP; i++) {
				VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i].renderFence));
				VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].swapchainSemaphore));

				_deletionQueue->push_function([=]() {
					vkDestroyFence(_device, _frames[i].renderFence, nullptr);
					vkDestroySemaphore(_device, _frames[i].swapchainSemaphore, nullptr);
					});
			}
		}

		void VulkanDevice::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {
			VkCommandBuffer cmd = _uploadContext.commandBuffer;

			VkCommandBufferBeginInfo cmdBeginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
			function(cmd);
			VK_CHECK(vkEndCommandBuffer(cmd));

			VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &cmd;

			VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _uploadContext.uploadFence));

			VK_CHECK(vkWaitForFences(_device, 1, &_uploadContext.uploadFence, VK_TRUE, UINT64_MAX));
			VK_CHECK(vkResetFences(_device, 1, &_uploadContext.uploadFence));
			VK_CHECK(vkResetCommandPool(_device, _uploadContext.commandPool, 0));
		}
	}
}