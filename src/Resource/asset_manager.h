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

    struct UploadStats {
        size_t lastSubmittedBytes{ 0 };
        double lastSubmitCpuMs{ 0.0 };
        uint64_t lastTimelineValue{ 0 };
    };

    struct StagingTask {
        uint64_t timelineValue; // �� GPU �ﵽ���ֵʱ...
        size_t size;            // ...�ͷ���ô���ֽڵĿռ�
        VkCommandBuffer cmdBufferToFree{ VK_NULL_HANDLE }; //���꼴�ٵĵ�ϻ
    };

    class AssetManager {
    public:
        static AssetManager& Get();

        void init(Aero::RHI::VulkanDevice* device);
        void cleanup();

        // δ������Ϊ�������첽��Ŀǰ�ȴͬ��ע����ļ���
        bool load_scene_sync(const std::string& name, const std::string& filePath);

        // ��ȡ��Դ (���� SceneRenderer ͨ����Щ�ӿ�������)
        std::optional<SceneData> get_scene(const std::string& name);

    private:
        AssetManager() = default;
        ~AssetManager() = default;
        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;

        Aero::RHI::VulkanDevice* _device{ nullptr };

        // ��Դע��� (������������Ϊδ����̨�̻߳�����д)
        std::mutex _assetMutex;
        std::unordered_map<std::string, SceneData> _loadedScenes;

        // �������ӣ������⡢Mesh�� ��

    public:
        AllocatedBuffer upload_buffer_async(size_t bufferSize, const void* data, VkBufferUsageFlags usage);
        uint64_t submit_async_uploads();
        AllocatedImage upload_image_async(int width, int height, VkFormat format, const void* pixels, size_t pixelSize);
        UploadStats get_upload_stats() const;

    private:
        //�����ݴ����ռ������
        size_t allocate_staging_space(size_t size);
        void update_staging_tail(); // ���� GPU �Ѷ�ȡ�Ŀռ�

        std::mutex _stagingMutex;
        std::deque<StagingTask> _stagingTasks;
        size_t _stagingUsedSpace{ 0 };
        size_t _pendingUploadBytes{ 0 };
        UploadStats _uploadStats;
    };
    
}
