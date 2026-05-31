#include "VulkanDevice.h"
#include <VkBootstrap.h>
#include <iostream>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Aero {
	namespace RHI {
		void VulkanDevice::init(Aero::Window* window, DeletionQueue& deletionQueue) {
			_deletionQueue = &deletionQueue;

			init_vulkan(window);
			init_swapchain(window);
			init_allocator();
			init_staging_ring_buffer(64 * 1024 * 1024); // 64 MB
			init_commands();
			init_sync_structures();
			_deletionQueue->push_function([this]() {
				cleanup_swapchain();
			});

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

			VkPhysicalDeviceVulkan13Features features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
			features13.dynamicRendering = VK_TRUE;
			features13.synchronization2 = VK_TRUE;

			VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
			features12.bufferDeviceAddress = VK_TRUE;
			features12.descriptorIndexing = VK_TRUE;
			features12.descriptorBindingPartiallyBound = VK_TRUE;
			features12.runtimeDescriptorArray = VK_TRUE;
			features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
			features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
			features12.timelineSemaphore = VK_TRUE;

			VkPhysicalDeviceFeatures baseFeatures{};
			baseFeatures.multiDrawIndirect = VK_TRUE;

			vkb::PhysicalDeviceSelector selector{vkb_inst};
			vkb::PhysicalDevice physicalDevice = selector
			                                         .set_minimum_version(1, 3)
			                                         .set_surface(_surface)
			                                         .set_required_features(baseFeatures)
			                                         .select()
			                                         .value();

			_chosenGPU = physicalDevice.physical_device;
			VkPhysicalDeviceProperties gpuProperties{};
			vkGetPhysicalDeviceProperties(_chosenGPU, &gpuProperties);
			_timestampPeriodNs = gpuProperties.limits.timestampPeriod;

			vkb::DeviceBuilder deviceBuilder{physicalDevice};
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
			vkb::SwapchainBuilder swapchainBuilder{_chosenGPU, _device, _surface};
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
		}

		void VulkanDevice::cleanup_swapchain() {
			for (VkImageView view : _swapchainImageViews) {
				vkDestroyImageView(_device, view, nullptr);
			}
			_swapchainImageViews.clear();
			_swapchainImages.clear();

			if (_swapchain != VK_NULL_HANDLE) {
				vkDestroySwapchainKHR(_device, _swapchain, nullptr);
				_swapchain = VK_NULL_HANDLE;
			}
		}

		void VulkanDevice::recreate_swapchain(Aero::Window* window) {
			int width = 0;
			int height = 0;
			glfwGetFramebufferSize(window->handle(), &width, &height);
			while (width == 0 || height == 0) {
				glfwWaitEvents();
				glfwGetFramebufferSize(window->handle(), &width, &height);
			}

			VK_CHECK(vkDeviceWaitIdle(_device));
			cleanup_swapchain();
			init_swapchain(window);
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
			VkCommandPoolCreateInfo commandPoolInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
			commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			commandPoolInfo.queueFamilyIndex = _graphicsQueueFamily;

			for (int i = 0; i < FRAME_OVERLAP; i++) {
				VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i].commandPool));

				VkCommandBufferAllocateInfo cmdAllocInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
				cmdAllocInfo.commandPool = _frames[i].commandPool;
				cmdAllocInfo.commandBufferCount = 1;
				cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

				VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].mainCommandBuffer));

				VkQueryPoolCreateInfo queryPoolInfo{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
				queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
				queryPoolInfo.queryCount = GPU_TIMESTAMP_QUERY_COUNT;
				VK_CHECK(vkCreateQueryPool(_device, &queryPoolInfo, nullptr, &_frames[i].timestampQueryPool));

				_deletionQueue->push_function([=]() {
					vkDestroyQueryPool(_device, _frames[i].timestampQueryPool, nullptr);
					vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);
				});
			}

			VkCommandPoolCreateInfo asyncPoolInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
			asyncPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			//这里用 Transfer Queue 所在的 Family
			asyncPoolInfo.queueFamilyIndex = _transferQueueFamily;
			VK_CHECK(vkCreateCommandPool(_device, &asyncPoolInfo, nullptr, &_asyncUploadContext.commandPool));

			VkCommandBufferAllocateInfo asyncAlloc{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
			asyncAlloc.commandPool = _asyncUploadContext.commandPool;
			asyncAlloc.commandBufferCount = 1;
			asyncAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			VK_CHECK(vkAllocateCommandBuffers(_device, &asyncAlloc, &_asyncUploadContext.commandBuffer));

			//唯一需要补充的：给新分配的弹匣上膛，进入录制状态
			VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			VK_CHECK(vkBeginCommandBuffer(_asyncUploadContext.commandBuffer, &beginInfo));

			_deletionQueue->push_function([=]() {
				vkDestroyCommandPool(_device, _asyncUploadContext.commandPool, nullptr);
			});
		}

		void VulkanDevice::init_sync_structures() {
			VkFenceCreateInfo fenceCreateInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
			fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			VkSemaphoreCreateInfo semaphoreCreateInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

			for (int i = 0; i < FRAME_OVERLAP; i++) {
				VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i].renderFence));
				VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].swapchainSemaphore));

				_deletionQueue->push_function([=]() {
					vkDestroyFence(_device, _frames[i].renderFence, nullptr);
					vkDestroySemaphore(_device, _frames[i].swapchainSemaphore, nullptr);
				});
			}
			VkSemaphoreTypeCreateInfo timelineCreateInfo{};
			timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
			timelineCreateInfo.pNext = nullptr;
			timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
			timelineCreateInfo.initialValue = 0;

			VkSemaphoreCreateInfo timelineSemaphoreInfo{};
			timelineSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			timelineSemaphoreInfo.pNext = &timelineCreateInfo;

			VK_CHECK(vkCreateSemaphore(_device, &timelineSemaphoreInfo, nullptr, &_asyncUploadContext.timelineSemaphore));

			_deletionQueue->push_function([=]() {
				vkDestroySemaphore(_device, _asyncUploadContext.timelineSemaphore, nullptr);
			});
		}

		void VulkanDevice::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {
			// 1. 分配一个临时的 Command Pool (必须在 Graphics Queue 上，因为初始化操作往往涉及图形屏障)
			VkCommandPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			poolInfo.queueFamilyIndex = _graphicsQueueFamily;

			VkCommandPool tempPool;
			VK_CHECK(vkCreateCommandPool(_device, &poolInfo, nullptr, &tempPool));

			// 2. 分配临时的 Command Buffer
			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = tempPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer cmd;
			VK_CHECK(vkAllocateCommandBuffers(_device, &allocInfo, &cmd));

			// 3. 开始录制
			VkCommandBufferBeginInfo cmdBeginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
			cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

			// 4. 执行用户的指令
			function(cmd);

			VK_CHECK(vkEndCommandBuffer(cmd));

			// 5. 提交给 Graphics Queue 并死等 (毕竟这是 immediate submit)
			VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmd;

			VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
			VkFence tempFence;
			VK_CHECK(vkCreateFence(_device, &fenceInfo, nullptr, &tempFence));

			// 提交并阻塞等待
			VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, tempFence));
			VK_CHECK(vkWaitForFences(_device, 1, &tempFence, true, 9999999999));

			// 6. 清理现场，不留痕迹
			vkDestroyFence(_device, tempFence, nullptr);
			vkDestroyCommandPool(_device, tempPool, nullptr);
		}

		void VulkanDevice::init_staging_ring_buffer(size_t size) {
			_stagingRingBuffer.totalSize = size;
			_stagingRingBuffer.head = 0;
			_stagingRingBuffer.tail = 0;

			VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			bufferInfo.size = size;
			bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; // 作为拷贝源

			VmaAllocationCreateInfo vmaallocInfo = {};
			vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			// 关键点 1: MAPPED_BIT 保证分配后 mappedData 直接可用，终生不调 vkMapMemory
			// 关键点 2: SEQUENTIAL_WRITE_BIT 提示驱动使用 Write-Combined 内存，提升 memcpy 极速
			vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

			VmaAllocationInfo allocInfo;
			VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &_stagingRingBuffer.buffer, &_stagingRingBuffer.allocation, &allocInfo));

			_stagingRingBuffer.mappedData = allocInfo.pMappedData;

			_deletionQueue->push_function([=]() {
				vmaDestroyBuffer(_allocator, _stagingRingBuffer.buffer, _stagingRingBuffer.allocation);
			});

			std::cout << "[RHI] Allocated " << size / (1024 * 1024) << "MB Persistent Staging Buffer." << std::endl;
		}
	} // namespace RHI
} // namespace Aero
