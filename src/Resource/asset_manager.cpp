#include "asset_manager.h"
#include "gltf_loader.h"
#include <iostream>
#include <chrono>
#include "RHI/vk_initializers.h"

namespace Aero::Resource {

    AssetManager& AssetManager::Get() {
        static AssetManager instance;
        return instance;
    }

    void AssetManager::init(Aero::RHI::VulkanDevice* device) {
        _device = device;
        std::cout << "[AssetManager] Initialized." << std::endl;
    }

    void AssetManager::cleanup() {
        std::scoped_lock multiLock(_assetMutex, _stagingMutex);

        // 1. 销毁遗留的 Command Buffer 弹匣
        for (auto& task : _stagingTasks) {
            if (task.cmdBufferToFree != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(_device->get_device(),
                    _device->get_async_upload_context().commandPool,
                    1, &task.cmdBufferToFree);
            }
        }
        _stagingTasks.clear();

        // 2. 清理资源注册表
        _loadedScenes.clear();
        std::cout << "[AssetManager] Cleaned up." << std::endl;
    }

    bool AssetManager::load_scene_sync(const std::string& name, const std::string& filePath) {
        std::optional<SceneData> sceneOpt = GLTFLoader::load_gltf(filePath);
        if (!sceneOpt.has_value()) {
            std::cerr << "[AssetManager] Failed to load scene: " << filePath << std::endl;
            return false;
        }

        std::lock_guard<std::mutex> lock(_assetMutex);
        _loadedScenes[name] = std::move(sceneOpt.value());

        // 这里只是解析了数据，还没有上传到 GPU
        // 下一步我们要把上传逻辑从 SceneRenderer 搬过来

        return true;
    }

    std::optional<SceneData> AssetManager::get_scene(const std::string& name) {
        std::lock_guard<std::mutex> lock(_assetMutex);
        auto it = _loadedScenes.find(name);
        if (it != _loadedScenes.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    UploadStats AssetManager::get_upload_stats() const {
        return _uploadStats;
    }

    void AssetManager::update_staging_tail() {
        uint64_t currentGpuTimeline = _device->get_timeline_value();

        std::lock_guard<std::mutex> lock(_stagingMutex);
        while (!_stagingTasks.empty() && _stagingTasks.front().timelineValue <= currentGpuTimeline) {
            auto& task = _stagingTasks.front();
            _stagingUsedSpace -= task.size;

            auto& ringBuffer = _device->get_staging_ring_buffer();
            ringBuffer.tail = (ringBuffer.tail + task.size) % ringBuffer.totalSize;

            if (task.cmdBufferToFree != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(_device->get_device(),
                    _device->get_async_upload_context().commandPool,
                    1, &task.cmdBufferToFree);
            }
            _stagingTasks.pop_front();
        }
    }

    size_t AssetManager::allocate_staging_space(size_t size) {
        auto& ringBuffer = _device->get_staging_ring_buffer();
        if (size > ringBuffer.totalSize) abort();

        std::unique_lock<std::mutex> lock(_stagingMutex);
        bool forcedSubmit = false;

        while (true) {
            lock.unlock();
            update_staging_tail(); //尝试回收已完成的空间
            lock.lock();

            bool hasSpace = (_stagingUsedSpace + size) <= ringBuffer.totalSize;
            bool contiguous = (ringBuffer.head + size) <= ringBuffer.totalSize;

            if (hasSpace) {
                if (contiguous) {
                    size_t offset = ringBuffer.head;
                    ringBuffer.head = (ringBuffer.head + size) % ringBuffer.totalSize;
                    _stagingUsedSpace += size;
                    return offset;
                }
                else {
                    // 发生折断：压入 padding，并将 head 归零
                    size_t padding = ringBuffer.totalSize - ringBuffer.head;
                    _stagingUsedSpace += padding;
                    ringBuffer.head = 0;
                    _stagingTasks.push_back({ _device->get_async_upload_context().uploadValue + 1, padding, VK_NULL_HANDLE });
                    // 注意这里没有 return，它会进入下一次 while 循环，在 head=0 处成功分配
                }
            }
            else {
                // 空间不足，打破死锁
                if (!forcedSubmit) {
                    lock.unlock();
                    submit_async_uploads();
                    lock.lock();
                    forcedSubmit = true;
                }
                std::this_thread::yield();
            }
        }
    }

    AllocatedBuffer AssetManager::upload_buffer_async(size_t bufferSize, const void* data, VkBufferUsageFlags usage) {
        auto vma = _device->get_allocator();

        VkBufferCreateInfo bufferInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = bufferSize;
        bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo vmaallocInfo = {};
        vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        AllocatedBuffer newBuffer;
        VK_CHECK(vmaCreateBuffer(vma, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation, nullptr));

        size_t stagingOffset = allocate_staging_space(bufferSize);
        auto& ringBuffer = _device->get_staging_ring_buffer();

        void* mappedPtr = (char*)ringBuffer.mappedData + stagingOffset;
        memcpy(mappedPtr, data, bufferSize);

        auto& uploadContext = _device->get_async_upload_context();
        std::lock_guard<std::mutex> uploadLock(uploadContext.uploadMutex);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = stagingOffset;
        copyRegion.dstOffset = 0;
        copyRegion.size = bufferSize;

        vkCmdCopyBuffer(uploadContext.commandBuffer, ringBuffer.buffer, newBuffer.buffer, 1, &copyRegion);

        {
            std::lock_guard<std::mutex> lock(_stagingMutex);
            _stagingTasks.push_back({ uploadContext.uploadValue + 1, bufferSize });
            _pendingUploadBytes += bufferSize;
        }

        return newBuffer;
    }

    AllocatedImage AssetManager::upload_image_async(int width, int height, VkFormat format, const void* pixels, size_t pixelSize) {
        VmaAllocator vma = _device->get_allocator();
        VkExtent3D imageExtent = { (uint32_t)width, (uint32_t)height, 1 };

        VkImageCreateInfo imageInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = imageExtent;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        AllocatedImage newImage;
        newImage.imageFormat = format;
        newImage.imageExtent = imageExtent;
        VK_CHECK(vmaCreateImage(vma, &imageInfo, &allocInfo, &newImage.image, &newImage.allocation, nullptr));

        VkImageViewCreateInfo viewInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = newImage.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(_device->get_device(), &viewInfo, nullptr, &newImage.view));

        size_t stagingOffset = allocate_staging_space(pixelSize);
        auto& ringBuffer = _device->get_staging_ring_buffer();
        void* mappedPtr = (char*)ringBuffer.mappedData + stagingOffset;
        memcpy(mappedPtr, pixels, pixelSize);

        auto& uploadContext = _device->get_async_upload_context();
        std::lock_guard<std::mutex> uploadLock(uploadContext.uploadMutex);

        vkinit::transition_image(uploadContext.commandBuffer, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = stagingOffset;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = imageExtent;

        vkCmdCopyBufferToImage(uploadContext.commandBuffer, ringBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        vkinit::transition_image(uploadContext.commandBuffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        {
            std::lock_guard<std::mutex> lock(_stagingMutex);
            _stagingTasks.push_back({ uploadContext.uploadValue + 1, pixelSize, VK_NULL_HANDLE });
            _pendingUploadBytes += pixelSize;
        }

        return newImage;
    }

    uint64_t AssetManager::submit_async_uploads() {
        auto submitStart = std::chrono::high_resolution_clock::now();
        auto& uploadContext = _device->get_async_upload_context();
        std::lock_guard<std::mutex> uploadLock(uploadContext.uploadMutex);

        VK_CHECK(vkEndCommandBuffer(uploadContext.commandBuffer));

        uploadContext.uploadValue++;
        uint64_t signalValue = uploadContext.uploadValue;

        VkTimelineSemaphoreSubmitInfo timelineInfo{ .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
        timelineInfo.signalSemaphoreValueCount = 1;
        timelineInfo.pSignalSemaphoreValues = &signalValue;

        VkSubmitInfo submitInfo = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.pNext = &timelineInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &uploadContext.commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &uploadContext.timelineSemaphore;

        VK_CHECK(vkQueueSubmit(_device->get_transfer_queue(), 1, &submitInfo, VK_NULL_HANDLE));

        VkCommandBuffer newCmd;
        VkCommandBufferAllocateInfo allocInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.commandPool = uploadContext.commandPool;
        allocInfo.commandBufferCount = 1;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VK_CHECK(vkAllocateCommandBuffers(_device->get_device(), &allocInfo, &newCmd));

        VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(newCmd, &beginInfo));

        {
            std::lock_guard<std::mutex> lock(_stagingMutex);
            _stagingTasks.push_back({ signalValue, 0, uploadContext.commandBuffer });
        }

        uploadContext.commandBuffer = newCmd;

        auto submitEnd = std::chrono::high_resolution_clock::now();
        _uploadStats.lastSubmitCpuMs = std::chrono::duration<double, std::milli>(submitEnd - submitStart).count();
        _uploadStats.lastSubmittedBytes = _pendingUploadBytes;
        _uploadStats.lastTimelineValue = signalValue;
        _pendingUploadBytes = 0;

        return signalValue; // 返回一个进度票据，主线程可以凭此票据查询是否加载完成
    }
}
