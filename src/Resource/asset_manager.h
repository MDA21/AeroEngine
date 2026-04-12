#pragma once
#include "RHI/vk_types.h";
#include "RHI/VulkanDevice.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <optional>
#include <deque>

namespace Aero::Resource {

    struct StagingTask {
        uint64_t timelineValue; // 当 GPU 达到这个值时...
        size_t size;            // ...释放这么多字节的空间
        VkCommandBuffer cmdBufferToFree{ VK_NULL_HANDLE }; //用完即毁的弹匣
    };

    class AssetManager {
    public:
        static AssetManager& Get();

        void init(Aero::RHI::VulkanDevice* device);
        void cleanup();

        // 未来将改为真正的异步，目前先搭建同步注册表的架子
        bool load_scene_sync(const std::string& name, const std::string& filePath);

        // 获取资源 (后续 SceneRenderer 通过这些接口拿数据)
        std::optional<SceneData> get_scene(const std::string& name);

    private:
        AssetManager() = default;
        ~AssetManager() = default;
        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;

        Aero::RHI::VulkanDevice* _device{ nullptr };

        // 资源注册表 (加锁保护，因为未来后台线程会往里写)
        std::mutex _assetMutex;
        std::unordered_map<std::string, SceneData> _loadedScenes;

        // 后续添加：纹理库、Mesh库 等

    public:
        AllocatedBuffer upload_buffer_async(size_t bufferSize, const void* data, VkBufferUsageFlags usage);
        uint64_t submit_async_uploads();
        AllocatedImage upload_image_async(int width, int height, VkFormat format, const void* pixels, size_t pixelSize);

    private:
        //环形暂存区空间分配器
        size_t allocate_staging_space(size_t size);
        void update_staging_tail(); // 回收 GPU 已读取的空间

        std::mutex _stagingMutex;
        std::deque<StagingTask> _stagingTasks;
        size_t _stagingUsedSpace{ 0 };
    };
    
}