# AeroEngine: 现代 Vulkan 渲染框架开发路线图 

**技术栈：** C++17/20, Vulkan 1.3, CMake, GPU-Driven Architecture
**开发周期：** 3个月



随手让ai生成的计划表，看个乐子

---

## 🛠️ 阶段 0：环境配置与工程架构 (Environment & Setup)

不要使用 VS 的 `.sln` 手动配置依赖，**必须使用 CMake**，这是工业界 C++ 项目的绝对标准。

### 1. 核心工具链
- [x] **IDE:** Visual Studio 2022 (安装 "C++ 桌面开发" 工作负载，确保勾选最新的 Windows 10/11 SDK)。
- [x] **构建系统:** CMake (版本 >= 3.20)。
- [x] **图形 API:** Vulkan SDK 1.3+ (包含 `glslc.exe` 用于编译 Shader)。
- [x] **性能分析:** 下载 NVIDIA Nsight Graphics / RenderDoc (用于后续截帧调试)。

### 2. 第三方库 (建议通过 Git Submodules 管理)
- [x] **Vulkan 拓展:** `volk` (Vulkan Meta-Loader，绕过静态链接，提升性能)、`VkBootstrap`。
- [x] **内存管理:** `VMA` (VulkanMemoryAllocator，AMD 开源的工业级显存分配器)。
- [x] **窗口系统:** `GLFW`。
- [x] **数学库:** `GLM` (注意：与 GPU 交互时严格使用 `alignas(16)` 或以 `vec4` 替代 `vec3` 保证内存对齐)。
- [x] **模型加载:** `cgltf` (纯 C 编写，单头文件，极速加载 glTF 2.0)。
- [x] **图像解析:** `stb_image`。
- [x] **GUI 工具:** `Dear ImGui` (Vulkan 后端)。

---

## 🗺️ 核心开发路线图 (The Roadmap)

### 📍 阶段 1：Vulkan 1.3 现代基建 (Week 1 - 3)
- [x] **核心上下文初始化：** - 使用 `volk` 加载 Vulkan 函数指针。
  - 创建 Instance, Physical Device, Logical Device, Swapchain。
  - **启用 1.3 核心特性：** 通过 `VkPhysicalDeviceVulkan13Features` 开启 Dynamic Rendering 和 Synchronization2，**拒绝使用旧版 Extensions**。
- [x] **显存管理接管：**
  - 集成 VMA，实现统一的 `Buffer` 和 `Image` 创建接口。
- [x] **Shader 编译管线：**
  - 在 CMake 中配置 Custom Command，在编译时自动调用 `glslc` 将 `.vert/.frag/.comp` 编译为 `SPIR-V` 二进制文件。
- [x] **告别 RenderPass：**
  - 使用 Dynamic Rendering API (`vkCmdBeginRendering`) 画出第一个彩色三角形。
- [x] **调试基建：**
  - 接入 Vulkan Validation Layers，封装自定义的 Debug Messenger（将警告输出到 VS 控制台）。
  - 集成 ImGui，能在画面上渲染一个 FPS 统计窗口。

### 📍 阶段 2：资产接管与无绑定架构 (Week 4 - 5)
- [x] **glTF 2.0 场景解析器：**
  - 使用 `cgltf` 解析包含层级关系（Scene Graph）的模型。
  - 提取 Vertex, Index, Normal, Tangent, UV 数据。
- [x] **Staging Buffer 与异步上传：**
  - 实现一个 UploadContext，利用专用的 Transfer Queue 将 CPU 数据拷贝至 GPU (Device Local) 显存。
- [x] **Bindless Architecture (无绑定架构)：**
  - 通过 `VkPhysicalDeviceVulkan12Features` 启用 Descriptor Indexing。
  - 全局只创建一个 `VkDescriptorSet`。
  - **实现：** 将场景所有的纹理存入一个 `Texture2D textures[]` 数组；将所有材质参数存入一个巨大的 SSBO。
- [x] **基础前向渲染 (Forward Rendering)：**
  - 在 Shader 中通过传入的 `Material ID` 动态采样对应的纹理，成功渲染带有基础贴图的 3D 场景（如 Sponza）。
- [x] **Camera封装和reverse-Z**

### 📍 阶段 3：终极杀器 —— GPU Driven 管线 (Week 6 - 8)
- [x] **场景数据打包 (DOD on GPU)：**
  - 将场景中所有实例的 `Transform` 矩阵和 `Bounding Box` 打包成一个紧凑的 SSBO (InstanceDataBuffer) 传给 GPU。（严格遵守 `std430` 和 `alignas(16)` 规范）。
- [ ] **Two-Pass Compute 剔除 (视锥 + Hi-Z 遮挡)：**
  - **Pass 1:** 视锥剔除与上一帧可见物体的 Hi-Z 遮挡剔除。
  - **Pass 2:** 生成深度金字塔 (Depth Pyramid)，对新物体进行第二遍精确剔除。
  
  只做了视锥体剔除
- [x] **Indirect Draw Buffer 生成：**
  - 剔除通过的线程，使用 `atomicAdd` 将实例参数写入到一个 `VkDrawIndexedIndirectCommand` 结构的 Buffer 中。
- [x] **一键绘制 (vkCmdDrawIndexedIndirect)：**
  - CPU 端彻底解放，仅调用一次 API 即可绘制成千上万个物体。
- [x] **性能验证 UI：**
  
  - 在 ImGui 中添加 Toggle 按钮对比：CPU 提交 vs GPU 驱动提交，并展示极端的帧率差异图。



###  阶段 3.5：引擎化重构 (基础设施补完 - 优先攻克)

- [ ] **代码物理拆分与系统解耦 (Project Structure)**

目前所有代码平铺在 `src` 下，我们需要按职责划分模块，建立真正的引擎目录树：

- **Core**: 窗口系统、输入处理、基础数学、日志。
  - **RHI (Render Hardware Interface)**: 把 `VulkanContext`、管线构建、交换链封装得更彻底，向上层隐藏 Vulkan 细节。
  - **Renderer**: 剥离 `AeroEngine::draw()` 中的逻辑，建立独立的渲染器系统（将 Compute Culling 和 Graphics Draw 拆分为独立的 Pass 函数）。
  - **Resource**: 负责模型、纹理的加载。

- [ ] **资源管理器 (Asset Manager)：**

  - **去硬编码：** 实现基于哈希/UUID 的资源索引，彻底消除代码中的绝对路径（如 `F:/...`）。

  - **引用计数：** 统一且自动地管理 Mesh、Material、Texture 的加载与生命周期。

- [ ] **异步上传系统 (Async Transfer)：**
  - 真正激活物理设备中的专用 `transferQueue`。
  - 使用 Timeline Semaphore 和 Staging Ring Buffer 实现后台资源流式加载，消除主线程卡顿。

- [ ] **渲染图谱 (Render Graph / Frame Graph)：**
  - 抽象 Pass 调度逻辑，由引擎自动推导 Image Layout Transition 和同步屏障 (Memory Barriers)。





### 📍 阶段 4：3A 级视觉与材质系统 (Week 9 - 10)
- [ ] **管线升级：Forward+ / Visibility Buffer：**
  
  - **架构抉择：** 放弃传统 Deferred，利用已有的 Bindless 优势实现高性能着色。
  
    **功能：** Tile/Cluster-based 光源剔除，完美兼容多光源与半透明物体渲染
- [ ] **工业标准 PBR 材质：**
  
  - 实现基于物理的微表面模型 (Cook-Torrance BRDF)。
- [ ] **IBL (基于图像的照明)：**
  - 解析 HDR 全景图。
  - 离线/加载时计算 Irradiance Map (漫反射) 和 Prefiltered Map (高光反射)。
  - 生成 BRDF LUT 纹理。
- [ ] **现代阴影系统：**
  - 实现 CSM (级联阴影贴图)，根据相机深度将视锥体分为 3-4 层，解决大场景阴影精度问题。
  - 使用 PCF (Percentage-Closer Filtering) 柔化阴影边缘。

### 📍 阶段 5：画面抛光与工程总结 (Week 11 - 12)
- [ ] **Tone Mapping (色调映射)：**
  - 接入 ACES Filmic Tone Mapping，防止 HDR 高光过曝。
- [ ] **Bloom (泛光后处理)：**
  - 利用 Compute Shader 实现高性能的降采样 (Downsample) 和升采样 (Upsample) 双边滤波泛光。
- [ ] **TAA (时域抗锯齿 - 加分挑战/风险项)：**
  - *视项目剩余时间决定是否实施，避免阻碍 1.0 版本发布。*
  - 生成 Velocity Buffer (速度图)。
  - 实现历史帧重投影与混合，消除锯齿和闪烁。
- [ ] **文档与宣发：**
  - 清理代码，完善 CMakeLists.txt。
  - 录制 4K 60FPS 演示视频（展示 10 万实例下的 GPU 驱动剔除和 PBR 光影）。
  - 撰写深度 Readme 和知乎技术博客。

---
> "The magic of computer graphics is that you can build the universe from scratch, provided you align your memory correctly."



