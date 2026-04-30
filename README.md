# AeroEngine

一个基于 `Vulkan 1.3` 的自研渲染引擎项目，当前主线是先完成现代图形引擎主干，再逐步演进为支持结构化 AI 工具调用和引擎语义理解的 `AI-extensible / AI-native` 编辑与运行框架。

## 项目定位

`AeroEngine` 不是一次性的渲染 Demo，而是一个长期演进项目：

- 短期目标：做出可投递的现代 Vulkan 渲染引擎
- 中期目标：做出可工具化控制的引擎宿主系统
- 长期目标：做出支持 AI Tool Use 和引擎语义上下文的 AI Native 雏形

这个项目同时服务两个方向：

- 图形方向：证明现代渲染架构、GPU-Driven 管线、资源系统与工程能力
- AI 方向：证明如何把 LLM 安全、可控地接入真实软件系统，而不是停留在聊天层

## 当前状态

基于当前代码，项目已经具备以下基础能力：

- Vulkan 1.3 设备与交换链初始化
- Dynamic Rendering
- Descriptor Indexing / Bindless 纹理思路
- Transfer Queue + Timeline Semaphore
- Persistent Staging Ring Buffer
- glTF CPU 侧解析与场景上传
- GPU-Driven / CPU-Driven 双路径
- Compute Frustum Culling
- ImGui 调试界面

当前代码结构已经初步拆分为：

- `src/Core`
- `src/RHI`
- `src/Renderer`
- `src/Resource`

## 技术栈

- `C++17/20`
- `Vulkan 1.3`
- `CMake`
- `GLFW`
- `VMA`
- `volk`
- `VkBootstrap`
- `GLM`
- `cgltf`
- `stb_image`
- `Dear ImGui`

## 项目结构

```text
AeroEngine/
├─ src/
│  ├─ Core/
│  ├─ RHI/
│  ├─ Renderer/
│  └─ Resource/
├─ docs/
├─ external/
├─ CMakeLists.txt
└─ README.md
```

## 路线文档

项目的正式规划文档已经放在 `docs` 目录：

- [长期里程碑规划](file:///f:/VSproject/AeroEngine/docs/milestones.md)
- [按月任务表](file:///f:/VSproject/AeroEngine/docs/monthly-plan.md)
- [项目管理版本](file:///f:/VSproject/AeroEngine/docs/project-management.md)

建议阅读顺序：

1. 先看 [长期里程碑规划](file:///f:/VSproject/AeroEngine/docs/milestones.md)
2. 再看 [按月任务表](file:///f:/VSproject/AeroEngine/docs/monthly-plan.md)
3. 最后看 [项目管理版本](file:///f:/VSproject/AeroEngine/docs/project-management.md)

## 当前开发主线

当前最优先的路线不是盲目堆新特性，而是：

1. 做强图形主干
2. 做强工具层
3. 接入 AI Tool Use
4. 建立引擎语义层
5. 再做本地模型与运行时专题

近期重点方向：

- `Swapchain Recreate`
- 更正式的 profiling 与统计
- `SceneRenderer` / `AssetManager` 职责继续收口
- `EngineCommandBus` / `EditorFacade`
- `Engine Context Snapshot`

## 阶段目标概览

### 阶段 1

- 形成可投递的现代 Vulkan 渲染引擎版本

### 阶段 2

- 建立命令系统、状态快照和编辑器工具层

### 阶段 3

- 接入结构化 AI Tool Use，形成自然语言到引擎命令的闭环

### 阶段 4

- 建立引擎语义上下文层，形成 AI Native 雏形

### 阶段 5

- 扩展混合推理、本地模型与运行时专题能力

## 项目原则

- 先保证每个阶段都能独立展示
- 先做 P0，再做 P1，最后做 P2
- 新功能只有在“能展示、能解释、能写进简历”时才算真正完成
- 不把最终系统建立在长期不可依赖的远程资源上

## 后续计划

接下来优先完成：

- 更稳定的渲染主干
- 更清晰的资源系统边界
- 更正式的文档、视频和 benchmark 记录
- 引擎工具化抽象

如果你只是想快速了解项目，建议直接从以下三个入口开始：

- [长期里程碑规划](file:///f:/VSproject/AeroEngine/docs/milestones.md)
- [按月任务表](file:///f:/VSproject/AeroEngine/docs/monthly-plan.md)
- [项目管理版本](file:///f:/VSproject/AeroEngine/docs/project-management.md)

