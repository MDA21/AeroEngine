# AeroEngine 长期里程碑规划

## 1. 项目定位

`AeroEngine` 的目标不是做一个短期 Demo，而是逐步演进为：

- 一个基于 Vulkan 1.3 的现代渲染引擎
- 一个具备编辑器工具能力的引擎宿主系统
- 一个支持结构化 AI 工具调用的 AI 扩展框架
- 一个最终具备引擎语义理解能力的 AI Native Editor/Runtime 雏形

这个路线必须同时满足两个条件：

- 长期主义：未来 2 到 3 年仍然有继续演进的空间
- 阶段成果：任意阶段结束时，都能形成可以展示、可以录屏、可以写进简历的可交付版本

---

## 2. 当前架构基线

基于当前代码，项目已经有比较清晰的分层基础：

- `Core`
  - `AeroEngine` 负责生命周期、主循环、输入、ImGui 调试界面
  - `Window` 负责 GLFW 窗口、事件、时间和输入
  - `Camera` 负责视角控制
- `RHI`
  - `VulkanDevice` 负责 Vulkan 实例、设备、交换链、命令池、同步对象、上传基础设施
- `Renderer`
  - `SceneRenderer` 负责场景资源上传、Compute Culling、Graphics Draw 和 descriptor 绑定
- `Resource`
  - `GLTFLoader` 负责 glTF CPU 侧解析
  - `AssetManager` 负责异步上传、staging ring buffer、transfer queue 和 timeline semaphore

当前已经具备的关键能力：

- Vulkan 1.3 基础设施
- Dynamic Rendering
- Descriptor Indexing / Bindless 纹理思路
- Transfer Queue + Timeline Semaphore
- Persistent Staging Ring Buffer
- glTF 解析与场景上传
- GPU-Driven 与 CPU 提交双路径
- Compute Frustum Culling
- ImGui 调试面板

当前还未完全收口的方向：

- `SceneRenderer` 和 `AssetManager` 的资源职责边界仍可继续梳理
- 缺少统一的命令系统和状态快照系统
- 缺少真正面向 AI 的工具接口层
- 缺少更正式的性能记录、错误诊断和项目文档体系

---

## 3. 总体路线

项目总体建议按如下主线推进：

1. 先做强图形主干
2. 再做强工具层
3. 再接入 AI Tool Use
4. 再做 AI Native 的语义层
5. 最后再做本地模型、运行时智能体和更研究化的方向

一句话概括：

> 先证明你能做真正的现代渲染引擎，再证明你能把它变成 AI 可用的系统。

---

## 4. 阶段 0：项目基线与展示体系

### 时间

- 现在到 2 周

### 目标

- 把长期项目变成可管理、可展示、可记录的工程

### 核心任务

- 明确 `RHI / Renderer / Resource / Editor / AI Layer` 的边界
- 建立 `docs/` 文档结构
- 建立 benchmark 记录方式
- 建立 demo 录制规范
- 整理截图、视频、架构图和实验记录目录

### 交付物

- `docs/milestones.md`
- `docs/monthly-plan.md`
- `docs/project-management.md`
- 一版架构图
- 一版基线性能表

### 验收标准

- 首次打开仓库的人可以快速理解项目定位、现状和下一步方向
- 你自己可以稳定复现一版基线 demo 和基线指标

### 简历价值

- 本阶段不单独写简历
- 但它决定你之后的成果能否高质量沉淀

---

## 5. 阶段 1：现代 Vulkan 渲染引擎可投递版本

### 时间

- 0 到 3 个月

### 目标

- 让项目先作为“现代 Vulkan 渲染引擎”成立

### 核心功能

- 稳定的窗口与交换链管理
- 场景加载与渲染主循环稳定运行
- CPU-Driven / GPU-Driven 双路径可切换
- 基础统计与调试 UI
- 更完善的资源生命周期管理
- 基础 profiling 能力

### 建议重点补齐

- `Swapchain Recreate`
  - resize 后正确重建 swapchain、depth image 和相关资源
- `Renderer Profiling`
  - CPU frame time
  - GPU frame time
  - culling 时间
  - 上传时间
- `Resource Ownership`
  - 梳理 `SceneRenderer` 与 `AssetManager` 的职责边界
- `Manual Reload`
  - 支持场景或 shader 的基础重载
- `Scene Diagnostics`
  - 当前场景 mesh、submesh、texture、material 统计

### 交付物

- 可稳定运行的场景 demo
- 至少 1 个大型 glTF 场景展示
- 一页性能与渲染状态调试面板
- 一篇图文技术总结
- 一段 1 到 2 分钟可用于投递的 demo 视频

### 验收标准

- 支持场景正常加载、绘制、相机移动
- 支持 CPU-Driven / GPU-Driven 切换
- 支持 resize 后继续稳定渲染
- 能输出关键性能统计
- 能录制完整、稳定的展示视频

### 简历价值

- 现代 Vulkan 渲染架构
- GPU-Driven Rendering
- Compute Culling
- 异步资源上传
- 工程化图形项目

---

## 6. 阶段 2：引擎工具化与命令系统

### 时间

- 3 到 6 个月

### 目标

- 从“能渲染”升级为“可被系统化控制”

### 核心功能

- `EditorFacade` 或 `EngineCommandBus`
- `Command Result` 统一格式
- 状态快照系统
- 命令日志面板
- 部分操作的撤销/回滚机制
- 更完整的编辑侧查询接口

### 第一批建议命令

- `load_scene(path)`
- `set_camera(...)`
- `toggle_gpu_driven(enabled)`
- `set_material_param(...)`
- `list_materials()`
- `list_renderables()`
- `get_scene_summary()`
- `get_engine_state()`
- `capture_frame()`

### 交付物

- 命令总线模块
- 引擎状态快照模块
- Scene / Resource / Render Mode 面板
- 命令日志和错误显示
- 至少 3 类操作支持撤销

### 验收标准

- 不直接碰底层代码，也能通过命令接口完成常见引擎操作
- 所有命令有统一成功/失败返回
- 常见错误可定位到模块级别
- 命令执行结果可回放、可审计

### 简历价值

- 引擎工具层设计
- Editor-facing API
- 可扩展架构
- AI 集成宿主环境

---

## 7. 阶段 3：AI Tool Use v1

### 时间

- 6 到 9 个月

### 目标

- 让 AI 真正接入引擎，但首先是可控的结构化工具调用

### 核心功能

- 接入一个主模型供应商
- 定义 `Aero Tool Schema`
- 实现模型到工具的 orchestration
- 实现 AI 控制面板
- 实现命令权限分级
- 实现失败恢复与用户确认机制

### 第一批 AI 能力

- 场景查询
- 相机控制
- 渲染模式切换
- 材质参数编辑
- 资源诊断
- 引擎状态解释

### 交付物

- AI 面板
- Tool Call 可视化
- 自然语言到引擎命令闭环
- 一段完整演示视频
- 一份工具 schema 文档

### 验收标准

- 至少 5 类只读任务稳定成功
- 至少 3 类安全编辑任务稳定成功
- 工具调用流程可见、可审计
- 错误调用不会破坏引擎状态

### 简历价值

- LLM 与自研图形引擎系统集成
- 自然语言驱动的编辑器能力
- 安全执行链路
- Tool Use 在真实软件系统中的落地

---

## 8. 阶段 4：引擎语义上下文与 AI Native 雏形

### 时间

- 9 到 12 个月

### 目标

- 从“AI 能调工具”升级到“AI 理解你的引擎”

### 核心功能

- 统一语义上下文层
- Scene / Material / Resource / Pipeline 的摘要系统
- 诊断型工具
- 截图与状态绑定分析流程
- 操作前后 diff 系统

### 交付物

- `Engine Semantic Context` 设计
- 诊断型 AI 工具
- before/after 状态摘要
- 一篇 AI Native 设计文档

### 验收标准

- AI 能理解当前场景基础状态
- AI 能处理至少 3 类诊断问题
- 回答基于结构化状态，不是单纯猜测
- 操作结果可以自动形成差异总结

### 简历价值

- 引擎语义层
- AI-assisted diagnostics
- 面向模型推理的数据抽象
- 向 AI Native Editor 演进的系统能力

---

## 9. 阶段 5：本地模型与混合推理

### 时间

- 12 到 18 个月

### 目标

- 利用本地 GPU 和远程算力，做有研究味但不破坏主系统可交付性的混合推理架构

### 核心功能

- 云端模型、本地小模型、规则系统协同
- 本地小模型用于轻量任务
- 远程超算用于离线实验和微调
- 模型调度和降级策略

### 本地模型优先切入点

- Tool intent classifier
- Command validation
- Scene summary compressor
- Error log tagger
- Resource issue triage

### 远程算力优先切入点

- Benchmark 对比实验
- LoRA/蒸馏
- embedding 构建
- 数据集清洗与生成

### 交付物

- 一版混合推理架构
- 一份实验报告
- 至少 1 个本地模型落地功能
- 一套降级运行方案

### 验收标准

- 主系统不依赖远程超算即可运行
- 本地模型真正参与任务链路
- 至少 1 份实验结果可以量化说明方案价值

### 简历价值

- Hybrid AI System
- 本地/云端协同
- 资源约束下的系统设计
- 实验与工程结合

---

## 10. 阶段 6：运行时 AI 与长期专题方向

### 时间

- 18 到 24 个月

### 目标

- 从编辑器辅助走向运行时智能能力

### 可选专题

- 运行时智能体 / NPC
- 视觉理解 + 渲染诊断
- 自然语言辅助场景构建
- 本地推理驱动运行时功能

### 交付物

- 至少选择一个专题做完整闭环
- 一份专题技术文档
- 一段专题展示视频

### 验收标准

- 不是一次性脚本，而是可重复运行的系统
- 同时有功能展示和指标支撑

### 简历价值

- 编辑态与运行时统一的 AI 能力
- 自研引擎中的智能系统实践

---

## 11. 阶段 7：长期终局方向

### 时间

- 24 个月以后

### 目标

- 形成你自己的 AI Native 引擎方法论和作品体系

### 可以追求的方向

- AI Native Editor
- 统一语义 Scene Graph
- Runtime + Editor 共享智能上下文
- 结构化状态与视觉状态联合推理
- 引擎内部原生推理接口

### 说明

- 这一阶段是长期演进目标，不应该反向干扰前面阶段的交付节奏

---

## 12. 每阶段推荐公开产出

- 阶段 1
  - 1 篇架构文
  - 1 个 demo 视频
- 阶段 2
  - 1 篇工具系统文
  - 1 张命令系统架构图
- 阶段 3
  - 1 个 AI Tool Use 演示
  - 1 篇工具 schema 设计文
- 阶段 4
  - 1 篇引擎语义层文章
- 阶段 5
  - 1 份混合推理实验报告
- 阶段 6
  - 1 个专题展示页或技术文章

---

## 13. 关键指标建议

### 图形指标

- 初始化时间
- 场景加载时间
- 上传耗时
- 帧时间
- CPU-Driven vs GPU-Driven 性能差异

### 工具层指标

- 命令执行成功率
- 回滚次数
- 平均执行时延
- 错误定位覆盖率

### AI 指标

- Tool call 成功率
- 平均回合数
- 人工干预次数
- 语义摘要大小
- 诊断正确率

### 本地模型指标

- 本地/云端路由比例
- 单任务显存占用
- 端到端响应时间
- 降级模式成功率

---

## 14. 一句话路线图

- 前 3 个月：做强图形主干
- 第 4 到 6 个月：做强工具层
- 第 7 到 9 个月：接入 AI Tool Use v1
- 第 10 到 12 个月：做引擎语义层
- 12 个月后：再做本地模型和运行时专题

这条路线的核心不是“最快做出一个大而全的 AI 引擎”，而是：

> 让每一个阶段都能独立成立、独立展示、独立写进简历，同时又服务于最终的长期目标。
