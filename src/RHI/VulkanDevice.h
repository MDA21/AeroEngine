#pragma once
#include "vk_types.h"
#include "Core/Window.h"
#include <vector>
#include <mutex>

namespace Aero {
	namespace RHI {
		struct FrameData
		{
			VkCommandPool commandPool;
			VkCommandBuffer mainCommandBuffer;
			VkSemaphore swapchainSemaphore;
			VkFence renderFence;
			VkQueryPool timestampQueryPool{ VK_NULL_HANDLE };
			bool hasValidTimestamps{ false };
		};

		inline constexpr uint32_t GPU_TIMESTAMP_QUERY_COUNT = 3;

		struct AsyncUploadContext {
			VkCommandPool commandPool;
			VkCommandBuffer commandBuffer;
			VkSemaphore timelineSemaphore; // Vulkan 1.2+ ʱ�����ź��������� Fence
			uint64_t uploadValue{ 0 };     // �α꣺��¼��ǰ���ύ�ĵ���ֵ
			std::mutex uploadMutex;        // ������δ���ڶ��߳�¼��ָ��ʱ�����ݾ���
		};

		struct StagingRingBuffer
		{
			VkBuffer buffer{ VK_NULL_HANDLE };
			VmaAllocation allocation{ VK_NULL_HANDLE };
			void* mappedData{ nullptr };

			size_t totalSize{ 0 };
			size_t head{ 0 };
			size_t tail{ 0 };
		};

		class VulkanDevice
		{
		public:
			void init(Aero::Window* window, DeletionQueue& deletionQueue);

			VkInstance get_instance()const { return _instance; }
			VkDevice get_device() const { return _device; }
			VkPhysicalDevice get_gpu() const { return _chosenGPU; }
			VmaAllocator get_allocator() const { return _allocator; }

			VkQueue get_graphics_queue() const { return _graphicsQueue; }
			uint32_t get_graphics_queue_family() const { return _graphicsQueueFamily; }
			VkQueue get_transfer_queue() const { return _transferQueue; }
			float get_timestamp_period_ns() const { return _timestampPeriodNs; }

			VkSwapchainKHR get_swapchain() const { return _swapchain; }
			VkFormat get_swapchain_format() const { return _swapchainImageFormat; }
			const std::vector<VkImage>& get_swapchain_images() const { return _swapchainImages; }
			const std::vector<VkImageView>& get_swapchain_image_views() const { return _swapchainImageViews; }
			void recreate_swapchain(Aero::Window* window);

			FrameData& get_current_frame();
			uint32_t get_frame_index() const { return _frameNumber % FRAME_OVERLAP; }
			void advance_frame() { _frameNumber++; }

			void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

			StagingRingBuffer& get_staging_ring_buffer() { return _stagingRingBuffer; }

			uint64_t get_timeline_value() const {
				uint64_t val;
				vkGetSemaphoreCounterValue(_device, _asyncUploadContext.timelineSemaphore, &val);
				return val;
			}

			AsyncUploadContext& get_async_upload_context() { return _asyncUploadContext; }

		private:
			void init_vulkan(Aero::Window* window);
			void init_swapchain(Aero::Window* window);
			void cleanup_swapchain();
			void init_allocator();
			void init_commands();
			void init_sync_structures();
			void init_staging_ring_buffer(size_t size);

			DeletionQueue* _deletionQueue{ nullptr };

			VkInstance _instance{ VK_NULL_HANDLE };
			VkDebugUtilsMessengerEXT _debugMessenger{ VK_NULL_HANDLE };
			VkSurfaceKHR _surface{ VK_NULL_HANDLE };
			VkPhysicalDevice _chosenGPU{ VK_NULL_HANDLE };
			VkDevice _device{ VK_NULL_HANDLE };

			VkQueue _graphicsQueue{ VK_NULL_HANDLE };
			uint32_t _graphicsQueueFamily{ 0 };
			VkQueue _transferQueue{ VK_NULL_HANDLE };
			uint32_t _transferQueueFamily{ 0 };
			float _timestampPeriodNs{ 1.0f };

			VkSwapchainKHR _swapchain{ VK_NULL_HANDLE };
			VkFormat _swapchainImageFormat;
			std::vector<VkImage> _swapchainImages;
			std::vector<VkImageView> _swapchainImageViews;

			VmaAllocator _allocator{ VK_NULL_HANDLE };

			FrameData _frames[FRAME_OVERLAP];
			uint32_t _frameNumber{ 0 };

			AsyncUploadContext  _asyncUploadContext;

			StagingRingBuffer _stagingRingBuffer;
		};

}
}
