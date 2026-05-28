# 阶段1 P1：资源生命周期清理 — SceneRenderer / AssetManager 职责边界重构

## 目标

将场景数据（顶点/索引/材质/纹理）的 GPU 上传逻辑从 `SceneRenderer` 移到 `AssetManager`。
`SceneRenderer` 只负责"怎么渲染"，`AssetManager` 负责"数据在哪"。

## 新数据流

```
AssetManager::load_scene("main", path)
  → GLTFLoader::load_gltf()   // CPU 解析
  → upload_scene()            // 上传到 GPU
  → 返回 GpuScene             // GPU 资源句柄集

SceneRenderer::bind_scene(gpuScene)
  → 生成 InstanceData + IndirectCommand
  → 更新 Bindless 描述符
```

---

## 第1步：数据结构定义（TODO #1 ~ #3）

### TODO #1 — vk_types.h — 新增 SceneStats

**文件**：`src/RHI/vk_types.h`
**位置**：`GPUMeshBuffers` 之后、`InstanceData` 之前
**难度**：⭐

**骨架代码**：
```cpp
struct SceneStats {
	uint32_t meshCount{ 0 };
	uint32_t submeshCount{ 0 };
	uint32_t materialCount{ 0 };
	uint32_t textureCount{ 0 };
	uint32_t vertexCount{ 0 };
	uint32_t indexCount{ 0 };
};
```

**说明**：把 `SceneRenderer.h` 中的 `SceneStats` 迁移到全局命名空间，使其成为共享类型。注意 `vk_types.h` 中所有类型都在全局命名空间（无 `Aero::` 前缀）。

**验证**：编译通过。

---

### TODO #2 — vk_types.h — 新增 GpuScene

**文件**：`src/RHI/vk_types.h`
**位置**：`SceneStats` 之后、`InstanceData` 之前
**难度**：⭐

**骨架代码**：
```cpp
struct GpuScene {
	GPUMeshBuffers meshBuffers;                // 顶点 + 索引
	AllocatedBuffer materialBuffer;            // MaterialParams SSBO
	std::vector<AllocatedImage> textures;      // 所有纹理（含 image + view）
	std::vector<SubMesh> subMeshes;            // CPU 端元数据（渲染器需要）
	std::vector<MaterialParams> materials;     // CPU 端材质数据（备用）
	SceneStats stats;

	[[nodiscard]] bool valid() const {
		// TODO #2: 至少 vertexBuffer 非空即视为有效
		// return meshBuffers.vertexBuffer.buffer != VK_NULL_HANDLE;
	}
};
```

**说明**：`GpuScene` 是 AssetManager 上传完成后返回的 GPU 资源句柄集。只包含"场景数据"相关的 GPU 资源。Instance/Indirect buffer 是渲染器产物，不属于这里。

**API 参考**：
| 类型 | 定义位置 | 说明 |
|------|---------|------|
| `GPUMeshBuffers` | vk_types.h | 顶点 + 索引 buffer 对 |
| `AllocatedBuffer` | vk_types.h | VkBuffer + VmaAllocation |
| `AllocatedImage` | vk_types.h | VkImage + VkImageView + VmaAllocation + Extent + Format |
| `SubMesh` | vk_types.h | firstIndex / indexCount / vertexOffset / materialIndex / AABB |
| `MaterialParams` | vk_types.h | baseColorFactor / pbrFactors / 4个纹理索引 |
| `VK_NULL_HANDLE` | Vulkan | 空句柄常量 |

**验证**：编译通过。`sizeof(GpuScene)` 约 200+ 字节。

---

### TODO #3 — SceneRenderer.h — 删除 SceneStats

**文件**：`src/Renderer/SceneRenderer.h`
**位置**：`namespace Aero::Renderer` 内部开头
**难度**：⭐

**操作**：删除以下 7 行：
```cpp
			struct SceneStats {
				uint32_t meshCount{ 0 };
				uint32_t submeshCount{ 0 };
				uint32_t materialCount{ 0 };
				uint32_t textureCount{ 0 };
				uint32_t vertexCount{ 0 };
				uint32_t indexCount{ 0 };
			};
```

**说明**：`SceneStats` 已迁移到 `vk_types.h`（全局命名空间）。C++ 名称查找规则：`Aero::Renderer` 内找不到 `SceneStats` → 查 `Aero` → 查全局 `::`，找到 `::SceneStats`。所以 `get_scene_stats()` 和 `SceneStats _sceneStats` 成员不需要修改。

**验证**：`cmake --build build` 编译通过。

---

## 第2步：AssetManager 接口层（TODO #4 ~ #6）

### TODO #4 — asset_manager.h — _loadedScenes 类型变更

**文件**：`src/Resource/asset_manager.h`
**位置**：`private` 区域，约第49行
**难度**：⭐⭐

**当前代码**：
```cpp
std::unordered_map<std::string, SceneData> _loadedScenes;
```

**改为**：
```cpp
std::unordered_map<std::string, GpuScene> _loadedScenes;
```

**说明**：注册表现在存储 GPU 端的场景资源，而不是 CPU 端解析数据。`GpuScene` 的默认析构不会释放 Vulkan 资源（VkBuffer/VkImage 是 raw handle），必须在 `unload_scene()` 中手动释放。

**API 参考**：
| API | 说明 |
|-----|------|
| `std::unordered_map<K,V>` | 哈希表，find() 返回迭代器，operator[] 插入或访问 |
| `VkBuffer` / `VkImage` | Vulkan raw handle，析构不会自动释放 |

**验证**：此时编译会在 `asset_manager.cpp` 报错（`_loadedScenes` 类型变了，但 .cpp 中还在用 `SceneData` 操作）。这是预期的——后续 TODO 会修复。

---

### TODO #5 — asset_manager.h — 声明 upload_scene

**文件**：`src/Resource/asset_manager.h`
**位置**：`public` 区域，`get_scene()` 之后
**难度**：⭐⭐

**骨架代码**：
```cpp
// ============================================================
// 核心：将 CPU 端的 SceneData 上传为 GPU 端的 GpuScene
// 只上传"场景数据"（顶点/索引/材质/纹理）
// Instance/Indirect buffer 不在此处理（属于渲染器职责）
// 注意：此方法记录上传命令但不提交——调用方负责 submit_async_uploads()
// ============================================================
std::optional<GpuScene> upload_scene(const SceneData& scene);
```

**说明**：这是从 `SceneRenderer::upload_scene()` 搬过来的核心逻辑。与原实现的区别是：不生成 InstanceData、不生成 IndirectCommand、不更新描述符、不推入 DeletionQueue。

**API 参考**：
| API | 说明 |
|-----|------|
| `std::optional<T>` | 可空值，has_value() 检查，value() 或 * 取值 |

**验证**：声明级别，暂无运行时验证。

---

### TODO #6 — asset_manager.h — 重构 load_scene / 新增 get_scene / unload_scene

**文件**：`src/Resource/asset_manager.h`
**位置**：`public` 区域，替换现有 `load_scene_sync` 和 `get_scene`
**难度**：⭐⭐

**当前代码**（删除）：
```cpp
bool load_scene_sync(const std::string& name, const std::string& filePath);
std::optional<SceneData> get_scene(const std::string& name);
```

**改为**：
```cpp
// 全流程场景加载：解析 glTF → 上传 GPU → 缓存到注册表
bool load_scene(const std::string& name, const std::string& filePath);

// 获取已加载场景的 GPU 资源句柄（非持有指针，返回 nullptr 表示未找到）
GpuScene* get_scene(const std::string& name);

// 卸载场景：销毁所有关联的 Vulkan 资源并从注册表移除
void unload_scene(const std::string& name);
```

**说明**：
- `load_scene` 名字从 `load_scene_sync` 简化（目前就是同步的）
- `get_scene` 返回指针而非 `optional<SceneData>` 副本——避免拷贝大量数据，也明确语义是"借用"而非"转移所有权"
- `unload_scene` 是新增的——旧代码没有显式卸载接口，场景资源在 cleanup 时随 map.clear() 一起销毁

**API 参考**：
| API | 说明 |
|-----|------|
| 裸指针 vs optional | 指针可为 nullptr，optional 有值语义（会拷贝）。GpuScene 不应该拷贝（含 Vulkan 句柄） |

**验证**：声明级别。编译会因 .cpp 中调用旧签名而报错——后续 TODO 修复。

---

## 第3步：AssetManager 实现（TODO #7 ~ #11）

### TODO #7 — asset_manager.cpp — 实现 upload_scene()

**文件**：`src/Resource/asset_manager.cpp`
**位置**：新增方法（放在 `submit_async_uploads()` 之前）
**难度**：⭐⭐⭐⭐
**预计行数**：~45 行

**骨架代码**：
```cpp
std::optional<GpuScene> AssetManager::upload_scene(const SceneData& scene) {
    GpuScene result;

    // 1. 填充 SceneStats（~6行）
    //    result.stats.meshCount = scene.meshCount;
    //    result.stats.submeshCount = static_cast<uint32_t>(scene.subMeshes.size());
    //    ...

    // 2. 上传顶点 Buffer（~5行）
    //    size_t vertexBufferSize = scene.vertices.size() * sizeof(Vertex);
    //    result.meshBuffers.vertexBuffer = upload_buffer_async(
    //        vertexBufferSize, scene.vertices.data(),
    //        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    // 3. 上传索引 Buffer（~4行）
    //    size_t indexBufferSize = scene.indices.size() * sizeof(uint32_t);
    //    result.meshBuffers.indexBuffer = upload_buffer_async(
    //        indexBufferSize, scene.indices.data(),
    //        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    // 4. 上传材质 Buffer（~4行）
    //    size_t materialBufferSize = scene.materials.size() * sizeof(MaterialParams);
    //    if (materialBufferSize > 0) {
    //        result.materialBuffer = upload_buffer_async(
    //            materialBufferSize, scene.materials.data(),
    //            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    //    }

    // 5. 上传纹理（~6行）
    //    for (uint32_t i = 0; i < scene.images.size(); i++) {
    //        const auto& img = scene.images[i];
    //        size_t pixelSize = img.width * img.height * 4;  // RGBA
    //        AllocatedImage tex = upload_image_async(
    //            img.width, img.height, VK_FORMAT_R8G8B8A8_UNORM,
    //            img.pixels, pixelSize);
    //        result.textures.push_back(tex);
    //    }

    // 6. 拷贝 CPU 端元数据（~3行）
    //    result.subMeshes = scene.subMeshes;
    //    result.materials = scene.materials;

    return result;
}
```

**关键提醒**：
- 参考原代码 `SceneRenderer.cpp:567-691` 中的 `upload_scene()`，但只搬"数据上传"部分
- 不搬：InstanceData 生成（610-629行）、IndirectCommand 生成（636-656行）、bindless 描述符更新（604行）、DeletionQueue cleanup lambda（664-688行）
- 性能注意：`result.subMeshes = scene.subMeshes` 会拷贝 vector。对于 Sponza（~400 submesh）没问题。未来大场景可考虑 move
- 不在内部调用 `submit_async_uploads()`——让调用方决定何时提交

**API 参考**：
| API | 用途 |
|-----|------|
| `upload_buffer_async(size, data, usage)` | 创建 GPU buffer + staging 拷贝，记录到 transfer CB |
| `upload_image_async(w, h, fmt, pixels, size)` | 创建 GPU image + view + staging 拷贝 + 布局转换 |
| `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT` | 顶点 buffer 用途标志 |
| `VK_BUFFER_USAGE_INDEX_BUFFER_BIT` | 索引 buffer 用途标志 |
| `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` | SSBO（Shader 读取/写入）用途标志 |
| `VK_FORMAT_R8G8B8A8_UNORM` | 8位 RGBA 纹理格式 |
| `sizeof(Vertex)` | 单个顶点的字节大小（48字节 = vec3+float+vec3+float+vec4） |

**验证**：编译通过。此时没有调用方，可以写一个简单的测试：在 `main.cpp` 中临时调用 `AssetManager::Get().upload_scene()` 看返回值。

---

### TODO #8 — asset_manager.cpp — 实现 unload_scene()

**文件**：`src/Resource/asset_manager.cpp`
**位置**：新增方法
**难度**：⭐⭐
**预计行数**：~20 行

**骨架代码**：
```cpp
void AssetManager::unload_scene(const std::string& name) {
    std::lock_guard<std::mutex> lock(_assetMutex);
    auto it = _loadedScenes.find(name);
    if (it == _loadedScenes.end()) return;

    GpuScene& scene = it->second;
    VmaAllocator allocator = _device->get_allocator();
    VkDevice device = _device->get_device();

    // 1. 销毁顶点/索引 Buffer（~4行）
    //    vmaDestroyBuffer(allocator,
    //        scene.meshBuffers.vertexBuffer.buffer,
    //        scene.meshBuffers.vertexBuffer.allocation);
    //    vmaDestroyBuffer(allocator,
    //        scene.meshBuffers.indexBuffer.buffer,
    //        scene.meshBuffers.indexBuffer.allocation);

    // 2. 销毁材质 Buffer（~3行）
    //    if (scene.materialBuffer.buffer != VK_NULL_HANDLE) {
    //        vmaDestroyBuffer(allocator,
    //            scene.materialBuffer.buffer,
    //            scene.materialBuffer.allocation);
    //    }

    // 3. 销毁纹理（~5行）
    //    for (auto& tex : scene.textures) {
    //        vkDestroyImageView(device, tex.view, nullptr);  // 先销毁 view
    //        vmaDestroyImage(allocator, tex.image, tex.allocation);  // 再销毁 image
    //    }

    // 4. 从注册表移除（~1行）
    //    _loadedScenes.erase(it);
}
```

**关键提醒**：
- 销毁顺序很重要：先 vkDestroyImageView，再 vmaDestroyImage（view 依赖 image）
- vmaDestroyBuffer/vmaDestroyImage 会自动处理 VMA 的内部追踪
- `_assetMutex` 保护 `_loadedScenes` 的并发访问

**API 参考**：
| API | 说明 |
|-----|------|
| `vmaDestroyBuffer(allocator, buffer, allocation)` | 销毁 VMA 分配的 buffer |
| `vmaDestroyImage(allocator, image, allocation)` | 销毁 VMA 分配的 image |
| `vkDestroyImageView(device, view, nullptr)` | 销毁 image view |
| `std::lock_guard<std::mutex>` | RAII 锁，构造时 lock，析构时 unlock |
| `unordered_map::find()` / `erase()` | 查找和删除元素 |

**验证**：编译通过。可以配合 TODO #9 一起验证——加载场景后卸载，检查无 Vulkan validation layer 报错。

---

### TODO #9 — asset_manager.cpp — 实现 load_scene()

**文件**：`src/Resource/asset_manager.cpp`
**位置**：替换原 `load_scene_sync()` 方法
**难度**：⭐⭐
**预计行数**：~15 行

**骨架代码**：
```cpp
bool AssetManager::load_scene(const std::string& name, const std::string& filePath) {
    // 1. CPU 解析 glTF（~3行）
    //    std::optional<SceneData> sceneOpt = GLTFLoader::load_gltf(filePath);
    //    if (!sceneOpt.has_value()) {
    //        std::cerr << "[AssetManager] Failed to load scene: " << filePath << std::endl;
    //        return false;
    //    }

    // 2. 上传到 GPU（~3行）
    //    std::optional<GpuScene> gpuScene = upload_scene(sceneOpt.value());
    //    if (!gpuScene.has_value()) {
    //        return false;
    //    }

    // 3. 缓存到注册表（~3行）
    //    {
    //        std::lock_guard<std::mutex> lock(_assetMutex);
    //        _loadedScenes[name] = std::move(gpuScene.value());
    //    }

    //    return true;
}
```

**关键提醒**：
- 用 `std::move` 将 GpuScene 移入 map（避免拷贝 vector 和 Vulkan 句柄）
- 旧方法名 `load_scene_sync` 改为 `load_scene`（目前就是同步的，未来再考虑异步）
- 旧代码中"这里只是解析了数据，还没有上传到 GPU"的注释现在可以删除了

**验证**：程序启动时不再报错（如果 aero_engine.cpp 已改为调用 load_scene）。

---

### TODO #10 — asset_manager.cpp — 实现 get_scene()

**文件**：`src/Resource/asset_manager.cpp`
**位置**：替换原 `get_scene()` 方法
**难度**：⭐
**预计行数**：~6 行

**骨架代码**：
```cpp
GpuScene* AssetManager::get_scene(const std::string& name) {
    std::lock_guard<std::mutex> lock(_assetMutex);
    auto it = _loadedScenes.find(name);
    if (it != _loadedScenes.end()) {
        return &it->second;  // 非持有指针
    }
    return nullptr;
}
```

**说明**：返回指针而非 `optional<GpuScene>`——避免拷贝，明确"借用"语义。调用方不应持有该指针超过 AssetManager 中场景的生存期。

**验证**：编译通过。

---

### TODO #11 — asset_manager.cpp — 更新 cleanup()

**文件**：`src/Resource/asset_manager.cpp`
**位置**：修改 `cleanup()` 方法
**难度**：⭐⭐
**预计行数**：~6 行

**当前代码**：
```cpp
_loadedScenes.clear();
```

**改为**：
```cpp
// 先销毁每个场景的 Vulkan 资源，再清空 map
for (auto& [name, scene] : _loadedScenes) {
    // TODO #11: 对每个场景调用 unload 中的销毁逻辑
    // 但不能直接调 unload_scene()（会修改 map 导致迭代器失效）
    // 手动销毁：meshBuffers, materialBuffer, textures
}
_loadedScenes.clear();
```

**关键提醒**：
- 不能在 range-for 遍历 map 时调用 `erase()`——这会使迭代器失效。两种方案：
  - A) 用 while + begin() 逐个 erase
  - B) 先收集所有 key，再逐个 unload_scene()
  - C) 在循环中只销毁资源，循环结束后 clear()
- 方案 C 最简单：遍历时手动 vmaDestroy*，最后 `_loadedScenes.clear()`

**验证**：程序退出时 validation layer 无资源泄漏报错。

---

## 第4步：SceneRenderer 改造（TODO #12 ~ #19）

### TODO #12 — SceneRenderer.h — 新增 bind_scene / 修改方法签名

**文件**：`src/Renderer/SceneRenderer.h`
**位置**：`public` 区域
**难度**：⭐⭐

**操作**：
1. 新增 `bind_scene()` 声明：
```cpp
void bind_scene(const GpuScene& gpuScene);
```

2. 修改 `submit_scene / reload_scene / reload_shaders_and_scene` 参数类型：
```cpp
// 旧签名（删除）：
bool submit_scene(const SceneData& scene, std::string* statusMessage = nullptr);
bool reload_scene(const SceneData& scene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);
bool reload_shaders_and_scene(const SceneData& scene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);

// 新签名：
bool submit_scene(const GpuScene& gpuScene, std::string* statusMessage = nullptr);
bool reload_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);
bool reload_shaders_and_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage = nullptr);
```

3. 修改 `update_global_descriptor_set()` 签名（加参数）：
```cpp
// 旧：
void update_global_descriptor_set();
// 新：
void update_global_descriptor_set(VkBuffer materialBuffer, VkBuffer instanceBuffer, VkBuffer indirectBuffer);
```

4. 保留 `upload_scene(const SceneData& scene)` 声明（向后兼容，后续删）

**验证**：编译会在 .cpp 中报签名不匹配的错误——后续 TODO 修复。

---

### TODO #13 — SceneRenderer.h — 成员变量调整

**文件**：`src/Renderer/SceneRenderer.h`
**位置**：`private` 区域底部
**难度**：⭐⭐

**操作**：

删除以下成员（它们属于 GpuScene，由 AssetManager 持有）：
```cpp
GPUMeshBuffers _mainMeshBuffers;          // → GpuScene::meshBuffers
AllocatedBuffer _materialBuffer;          // → GpuScene::materialBuffer
std::vector<AllocatedImage> _sceneTextures; // → GpuScene::textures
std::vector<SubMesh> _renderables;        // → GpuScene::subMeshes
SceneStats _sceneStats;                   // → GpuScene::stats
```

保留以下成员（渲染器私有）：
```cpp
AllocatedBuffer _instanceBuffer;      // 渲染器生成的 InstanceData SSBO
AllocatedBuffer _drawIndirectBuffer;  // 渲染器生成的 Indirect Draw Buffer
uint32_t _instanceCount{ 0 };         // 实例数量
```

新增：
```cpp
const GpuScene* _currentScene{ nullptr };  // 非持有指针
```

**关键提醒**：`_currentScene` 是裸指针，不负责销毁。它指向 `AssetManager::_loadedScenes` 中的 GpuScene。如果 AssetManager 在渲染期间 unload 场景，会导致悬垂指针 → UB。当前通过 `vkDeviceWaitIdle()` 保护。

**验证**：编译。`.cpp` 中所有引用 `_mainMeshBuffers` / `_materialBuffer` / `_sceneTextures` / `_renderables` / `_sceneStats` 的地方都会报错——这是预期行为，后续 TODO 逐个修复。

---

### TODO #14 — SceneRenderer.cpp — 实现 bind_scene()

**文件**：`src/Renderer/SceneRenderer.cpp`
**位置**：新增方法（放在 `submit_scene` 之前）
**难度**：⭐⭐⭐⭐
**预计行数**：~60 行

**骨架代码**：
```cpp
void SceneRenderer::bind_scene(const GpuScene& gpuScene) {
    _currentScene = &gpuScene;
    _instanceCount = static_cast<uint32_t>(gpuScene.subMeshes.size());

    auto& assetManager = Aero::Resource::AssetManager::Get();

    // --- 1. 更新 Bindless 纹理描述符 ---
    // TODO #14a: 遍历 gpuScene.textures，对每个纹理调用 update_bindless_texture
    // for (uint32_t i = 0; i < gpuScene.textures.size(); i++) {
    //     update_bindless_texture(gpuScene.textures[i], i);
    // }
    // (~3行)

    // --- 2. 生成 InstanceData ---
    // TODO #14b: 遍历 gpuScene.subMeshes，为每个 SubMesh 生成一个 InstanceData
    // - modelMatrix = glm::mat4(1.0f)（单位矩阵，无变换）
    // - 用 memcpy 把 materialIndex（uint32_t）转成 float 存入 aabbMin_MatID.w
    // - aabbMax_Pad.w = 0.0f
    // - 其他字段按 InstanceData 结构体填充
    // 参考原 upload_scene 中 610-629 行
    // (~15行)

    // --- 3. 生成 Indirect Draw Commands ---
    // TODO #14c: 遍历 gpuScene.subMeshes，为每个生成 VkDrawIndexedIndirectCommand
    // - indexCount = sm.indexCount
    // - instanceCount = 1（默认绘制，culling compute 会改为 0）
    // - firstIndex = sm.firstIndex
    // - vertexOffset = sm.vertexOffset
    // - firstInstance = i（对应 InstanceData 索引）
    // 参考原 upload_scene 中 636-656 行
    // (~10行)

    // --- 4. 上传 Instance/Indirect Buffer ---
    // TODO #14d: 通过 AssetManager 上传
    // instance buffer: VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    // indirect buffer: VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    // (~8行)

    // --- 5. 提交上传并等待 GPU 完成 ---
    // TODO #14e: submit_async_uploads() + vkWaitSemaphores（timeline semaphore）
    // (~12行，参考原 submit_scene 中 112-124 行)
    // 关键：_renderDevice->get_async_upload_context().timelineSemaphore
    //       VkSemaphoreWaitInfo + vkWaitSemaphores

    // --- 6. 更新全局描述符 ---
    // TODO #14f: 调用 update_global_descriptor_set(
    //     gpuScene.materialBuffer.buffer,
    //     _instanceBuffer.buffer,
    //     _drawIndirectBuffer.buffer);
    // (~3行)

    // --- 7. 注册清理回调 ---
    // TODO #14g: 将 instance/indirect buffer 的销毁逻辑推入 _deletionQueue
    // 只销毁 _instanceBuffer 和 _drawIndirectBuffer
    // 不销毁场景资源（由 AssetManager 管理）
    // (~10行，参考原 upload_scene 的 DeletionQueue lambda)
}
```

**API 参考**：
| API | 用途 |
|-----|------|
| `vkWaitSemaphores(device, &waitInfo, UINT64_MAX)` | CPU 端等待 timeline semaphore |
| `VkSemaphoreWaitInfo` | `.sType`, `.semaphoreCount`, `.pSemaphores`, `.pValues` |
| `VK_CHECK(x)` | 项目宏，检查 VkResult 并 abort |
| `VkDrawIndexedIndirectCommand` | `{ indexCount, instanceCount, firstIndex, vertexOffset, firstInstance }` |
| `vkUpdateDescriptorSets` | 批量更新描述符集 |
| `memcpy(&floatDest, &uint32Src, sizeof(float))` | 把 uint32_t 按位拷贝为 float（shader 中用 floatBitsToUint 还原） |

**验证**：此方法依赖多个其他 TODO 完成后才能调用。单独验证：编译通过，逻辑正确性等全链路跑通后验证。

---

### TODO #15 — SceneRenderer.cpp — 修改 update_global_descriptor_set()

**文件**：`src/Renderer/SceneRenderer.cpp`
**位置**：现 `update_global_descriptor_set()` 方法（约501-547行）
**难度**：⭐⭐
**预计行数**：~40 行（与原实现相似）

**改动**：
1. 函数签名改为参数形式（已在 TODO #12 中修改声明）
2. 函数体内，把原来从成员变量 `_materialBuffer.buffer` / `_instanceBuffer.buffer` / `_drawIndirectBuffer.buffer` 读取，改为使用参数

```cpp
void SceneRenderer::update_global_descriptor_set(
    VkBuffer materialBuffer,
    VkBuffer instanceBuffer,
    VkBuffer indirectBuffer) {

    // 与原实现相同的 3 组 VkDescriptorBufferInfo + VkWriteDescriptorSet
    // 区别：VkDescriptorBufferInfo::buffer = 参数而非成员变量
    // 参考原实现 SceneRenderer.cpp:501-547
}
```

**API 参考**：
| API | 用途 |
|-----|------|
| `VkDescriptorBufferInfo` | `.buffer`, `.offset=0`, `.range=VK_WHOLE_SIZE` |
| `VkWriteDescriptorSet` | `.sType`, `.dstSet`, `.dstBinding`, `.descriptorType`, `.descriptorCount`, `.pBufferInfo` |
| `vkUpdateDescriptorSets(device, count, writes, 0, nullptr)` | 批量更新 |

**验证**：编译通过。

---

### TODO #16 — SceneRenderer.cpp — 修改 cleanup()

**文件**：`src/Renderer/SceneRenderer.cpp`
**位置**：`cleanup()` 方法
**难度**：⭐⭐
**预计行数**：~12 行

**改动**：`cleanup()` 不再销毁场景资源（mesh/material/texture），只销毁渲染器私有资源。

```cpp
void SceneRenderer::cleanup() {
    // TODO #16:
    // 1. 如果 _instanceBuffer.buffer 非空，vmaDestroyBuffer（~3行）
    // 2. 如果 _drawIndirectBuffer.buffer 非空，vmaDestroyBuffer（~3行）
    // 3. destroy_depth_image()（已有）
    // 4. _deletionQueue.flush()（销毁管线/布局/描述符等）
    // 5. _currentScene = nullptr（~1行）
    //
    // 注意：不再销毁 meshBuffers / materialBuffer / sceneTextures
    //      这些现在由 AssetManager 管理
}
```

**关键提醒**：旧代码中 `_deletionQueue.flush()` 会触发 lambda 中捕获的 `_mainMeshBuffers` 等成员的销毁。现在这些成员已移除，对应的清理也不再需要。但 Instance/Indirect buffer 的销毁 lambda 在 bind_scene 中推入（TODO #14g），flush 时会正确执行。

**验证**：编译通过，逻辑正确性等全链路跑通后验证（退出时无 validation layer 报错）。

---

### TODO #17 — SceneRenderer.cpp — 修改 get_scene_stats()

**文件**：`src/Renderer/SceneRenderer.cpp`
**位置**：`get_scene_stats()` 方法
**难度**：⭐⭐
**预计行数**：~5 行

**改动**：原来 `_sceneStats` 是成员变量，现在需要从 `_currentScene->stats` 读取。

```cpp
const SceneStats& SceneRenderer::get_scene_stats() const {
    // TODO #17:
    // static SceneStats emptyStats{};  // 默认空统计
    // if (_currentScene != nullptr) {
    //     return _currentScene->stats;
    // }
    // return emptyStats;
}
```

**关键提醒**：返回 `const&` 需要确保引用的对象活着。`_currentScene->stats` 指向 AssetManager 中的 GpuScene，只要场景不被卸载就一直有效。如果没有绑定场景（`_currentScene == nullptr`），返回一个 static 空对象。

**验证**：编译通过。

---

### TODO #18 — SceneRenderer.cpp — 修改 submit / reload / reload_shaders

**文件**：`src/Renderer/SceneRenderer.cpp`
**位置**：`submit_scene()`, `reload_scene()`, `reload_shaders_and_scene()` 方法
**难度**：⭐⭐⭐
**预计行数**：~25 行（三个方法合计）

**改动**：这些方法的参数从 `SceneData&` 变为 `GpuScene&`，内部逻辑简化——GpuScene 已经是上传好的，只需 bind。

```cpp
bool SceneRenderer::submit_scene(const GpuScene& gpuScene, std::string* statusMessage) {
    // TODO #18a:
    // 1. bind_scene(gpuScene)
    // 2. if (statusMessage) *statusMessage = "Ready";
    // 3. return true;
    //
    // 注意：不再需要 upload_scene() + submit_async_uploads() + vkWaitSemaphores
    //     因为 GpuScene 已经是上传好的（由 AssetManager 管理上传流程）
    //     但 bind_scene() 内部会上传 instance/indirect buffer 并等待
    // (~8行)
}

bool SceneRenderer::reload_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage) {
    // TODO #18b:
    // 1. VK_CHECK(vkDeviceWaitIdle(_renderDevice->get_device()));
    // 2. cleanup();
    // 3. init(_renderDevice, windowWidth, windowHeight);  // 重建管线
    // 4. bind_scene(gpuScene);  // 绑定新场景
    // 5. if (statusMessage) *statusMessage = "Scene reloaded";
    // 6. return true;
    // (~10行)
}

bool SceneRenderer::reload_shaders_and_scene(const GpuScene& gpuScene, uint32_t windowWidth, uint32_t windowHeight, std::string* statusMessage) {
    // TODO #18c:
    // 1. if (!recompile_shader_binaries(statusMessage)) return false;
    // 2. reload_scene(gpuScene, windowWidth, windowHeight, statusMessage);
    // (~6行)
}
```

**API 参考**：
| API | 用途 |
|-----|------|
| `vkDeviceWaitIdle(device)` | 等待 GPU 完成所有工作——重载前必须调用 |
| `recompile_shader_binaries(statusMessage)` | 运行时调用 glslc 重编译着色器（已有函数） |

**验证**：编译通过。

---

### TODO #19 — SceneRenderer.cpp — 修改 draw()

**文件**：`src/Renderer/SceneRenderer.cpp`
**位置**：`draw()` 方法（约152-223行）
**难度**：⭐⭐⭐
**预计行数**：~60 行（逻辑与原实现相同，只改数据来源）

**改动**：所有原来从 `_mainMeshBuffers` / `_materialBuffer` / `_renderables` 读取的地方，改为从 `_currentScene` 读取。

```cpp
void SceneRenderer::draw(VkCommandBuffer cmd, VkImageView targetImageView, const Camera& camera,
                         uint32_t screenWidth, uint32_t screenHeight, bool useGPUDriven,
                         VkQueryPool timestampQueryPool) {
    // TODO #19:
    // if (_currentScene == nullptr || !_currentScene->valid()) return;
    //
    // 计算 viewProj（不变）
    // Compute Culling（不变，实例数据来源不变——仍是 _instanceBuffer）
    // 写 timestamp #1（不变）
    // BeginRendering（不变）
    //
    // 关键变更——绑定 VB/IB：
    // 旧: _mainMeshBuffers.vertexBuffer.buffer
    // 新: _currentScene->meshBuffers.vertexBuffer.buffer
    //
    // 旧: _mainMeshBuffers.indexBuffer.buffer
    // 新: _currentScene->meshBuffers.indexBuffer.buffer
    //
    // CPU fallback 路径：
    // 旧: _renderables[i % _renderables.size()]
    // 新: _currentScene->subMeshes[i]
    //
    // 绑定 pipeline / descriptor set / push constant（不变）
    // GPU-driven: vkCmdDrawIndexedIndirect（不变）
    // CPU-driven: vkCmdDrawIndexed loop（改 subMeshes 来源）
    // EndRendering（不变）
}
```

**关键提醒**：
- `_instanceBuffer` 和 `_drawIndirectBuffer` 仍然是 `SceneRenderer` 的成员，draw 中引用不变
- `_currentScene` 必须在 draw 之前被 bind_scene() 设置
- 性能：改后不增加任何 GPU 开销，只是 CPU 侧指针间接访问

**API 参考**：
| API | 用途 |
|-----|------|
| `vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset)` | 绑定顶点 buffer |
| `vkCmdBindIndexBuffer(cmd, buffer, 0, VK_INDEX_TYPE_UINT32)` | 绑定索引 buffer |
| `vkCmdDrawIndexedIndirect(cmd, buffer, 0, count, stride)` | GPU-driven 间接绘制 |
| `vkCmdDrawIndexed(cmd, indexCount, 1, firstIndex, vertexOffset, i)` | CPU-driven 绘制 |

**验证**：编译通过，程序运行后 Sponza 正常渲染。

---

## 第5步：上层连接（TODO #20 ~ #21）

### TODO #20 — aero_engine.cpp — init() 改用新流程

**文件**：`src/Core/aero_engine.cpp`
**位置**：`init()` 方法中场景加载部分（约51-60行）
**难度**：⭐⭐
**预计行数**：~12 行

**骨架代码**：
```cpp
// 旧流程（删除）：
// std::optional<SceneData> sceneOpt = GLTFLoader::load_gltf(_currentScenePath);
// if (sceneOpt.has_value()) {
//     _sceneRenderer->submit_scene(sceneOpt.value(), &_reloadStatus);
// }

// TODO #20: 新流程
// 1. AssetManager::Get().load_scene("main", _currentScenePath)
//    → 解析 glTF + 上传 GPU + 缓存
// 2. 如果失败，设置 _reloadStatus 并输出错误
// 3. GpuScene* gpuScene = AssetManager::Get().get_scene("main")
// 4. 如果非空，_sceneRenderer->bind_scene(*gpuScene)
// 5. 设置 _reloadStatus = "Ready"
```

**关键提醒**：
- 去掉对 `GLTFLoader.h` 的直接依赖（AeroEngine 不再直接调用 GLTFLoader）
- `#include "Resource/gltf_loader.h"` 可以移除（或保留，等清理时再删）
- AssetManager::load_scene 内部调用 GLTFLoader，AeroEngine 不需要知道

**API 参考**：
| API | 用途 |
|-----|------|
| `AssetManager::Get().load_scene(name, path)` | 全流程加载 |
| `AssetManager::Get().get_scene(name)` | 获取已加载场景的指针 |

**验证**：编译通过，程序启动后 Sponza 正常渲染。

---

### TODO #21 — aero_engine.cpp — 热重载适配

**文件**：`src/Core/aero_engine.cpp`
**位置**：`process_reload_requests()` 方法（约67-93行）
**难度**：⭐⭐
**预计行数**：~15 行

**骨架代码**：
```cpp
void AeroEngine::process_reload_requests() {
    if (!_pendingSceneReload && !_pendingShaderReload) return;

    // TODO #21:
    // 1. 先从 AssetManager 卸载旧场景
    //    AssetManager::Get().unload_scene("main");
    //
    // 2. 重新加载
    //    AssetManager::Get().load_scene("main", _currentScenePath);
    //
    // 3. 获取新场景
    //    GpuScene* gpuScene = AssetManager::Get().get_scene("main");
    //    if (!gpuScene) { _reloadStatus = "Failed"; ... return; }
    //
    // 4. 按类型重载
    //    if (_pendingShaderReload): _sceneRenderer->reload_shaders_and_scene(...)
    //    else if (_pendingSceneReload): _sceneRenderer->reload_scene(...)
    //
    // 5. 重置标志位
    //    _pendingShaderReload = false; _pendingSceneReload = false;
}
```

**关键提醒**：
- 不再直接调用 `GLTFLoader::load_gltf()`——AssetManager 内部处理
- `reload_scene` / `reload_shaders_and_scene` 的参数现在是 `GpuScene&`

**验证**：编译通过，ImGui 的 Reload Scene / Reload Shaders 按钮正常工作。

---

## 全链路验证清单

所有 TODO 完成后，按以下步骤验证：

1. **编译**：`cmake --build build` 无错误
2. **启动**：程序正常启动，Sponza 场景渲染正确
3. **相机**：WASD + 鼠标旋转正常
4. **ImGui 面板**：场景统计（mesh/submesh/material/texture 数量）正常显示
5. **GPU/CPU 切换**：Enable GPU-Driven Rendering 切换正常
6. **Reload Scene**：点击后面面刷新，无崩溃
7. **Reload Shaders**：点击后着色器重编译，画面刷新
8. **窗口 Resize**：拖动窗口边缘，不崩溃
9. **退出**：正常退出，Vulkan validation layer 无资源泄漏报错
10. **Benchmark**：帧时间与重构前对比，无明显性能退化（< 5%）
