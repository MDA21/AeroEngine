#pragma once
#include "RHI/vk_types.h"
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
		size_t lastSubmittedBytes{0};
		double lastSubmitCpuMs{0.0};
		uint64_t lastTimelineValue{0};
	};

	struct StagingTask {
		uint64_t timelineValue; // �� GPU �ﵽ���ֵʱ...
		size_t size; // ...�ͷ���ô���ֽڵĿռ�
		VkCommandBuffer cmdBufferToFree{VK_NULL_HANDLE}; //���꼴�ٵĵ�ϻ
	};

	class AssetManager {
	public:
		static AssetManager& Get();

		void init(Aero::RHI::VulkanDevice* device);
		void cleanup();

		// 全流程场景加载：解析 glTF → 上传 GPU → 缓存到注册表
		bool load_scene(const std::string& name, const std::string& filePath);

		// 获取已加载场景的 GPU 资源句柄（非持有指针，nullptr 表示未找到）
		GpuScene* get_scene(const std::string& name);

		// 卸载场景：销毁所有关联的 Vulkan 资源并从注册表移除
		void unload_scene(const std::string& name);

		// 将 CPU 端 SceneData 上传为 GPU 端 GpuScene
		// 只上传场景数据（顶点/索引/材质/纹理），不处理 Instance/Indirect
		// 注意：此方法只记录上传命令，不提交——调用方负责 submit_async_uploads()
		std::optional<GpuScene> upload_scene(const SceneData& scene);

	private:
		AssetManager() = default;
		~AssetManager() = default;
		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;

		Aero::RHI::VulkanDevice* _device{nullptr};

		// ��Դע��� (������������Ϊδ����̨�̻߳�����д)
		std::mutex _assetMutex;
		std::unordered_map<std::string, GpuScene> _loadedScenes;

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
		size_t _stagingUsedSpace{0};
		size_t _pendingUploadBytes{0};
		UploadStats _uploadStats;
	};

} // namespace Aero::Resource
