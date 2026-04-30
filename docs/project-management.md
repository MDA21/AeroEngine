# AeroEngine 项目管理版本

## 1. 文档目标

这份文档不是讲愿景，而是用于项目管理和执行跟踪。

使用方式：

- 按阶段管理
- 按优先级拆分
- 每个任务明确状态
- 每个阶段结束后做一次验收

建议状态：

- `TODO`
- `DOING`
- `BLOCKED`
- `DONE`
- `DEFERRED`

---

## 2. 项目主线

### P0 主线

- 做出可投递的现代 Vulkan 渲染引擎
- 做出可被系统化控制的引擎工具层
- 做出结构化 AI Tool Use v1

### P1 主线

- 做出引擎语义上下文层
- 做出 AI 诊断能力
- 做出混合推理架构

### P2 主线

- 做出运行时 AI 专题方向
- 做本地模型与更研究化路线
- 做更长期的 AI Native Editor/Runtime 方法论

---

## 3. 阶段划分

- 阶段 1：渲染引擎可投递版本
- 阶段 2：工具层与命令系统
- 阶段 3：AI Tool Use v1
- 阶段 4：语义层与 AI Native 雏形
- 阶段 5：混合推理与运行时专题

---

## 4. 阶段 1：渲染引擎可投递版本

### 阶段目标

- 完成现代 Vulkan 渲染引擎主干，并形成稳定 demo 与可投递材料

### P0

- [x] `Swapchain Recreate`
  - 说明：支持窗口 resize 后正确重建 swapchain 及相关资源
  - 状态：DONE
- [x] `Depth/Image 依赖重建`
  - 说明：swapchain 重建时同步重建 depth image 与必要 attachment
  - 状态：DONE
- [x] `基础性能统计`
  - 说明：输出 CPU frame time、GPU frame time、culling 时间、上传时间
  - 状态：DONE
- [x] `场景统计 UI`
  - 说明：显示 mesh、submesh、material、texture 统计
  - 状态：DONE
- [ ] `稳定 demo 录制版本`
  - 说明：形成 1 到 2 分钟正式演示视频
  - 状态：TODO

### P1

- [ ] `Shader/Scene 手动重载`
  - 状态：TODO
- [ ] `资源生命周期清理`
  - 说明：理顺 `SceneRenderer` 与 `AssetManager` 的职责边界
  - 状态：TODO
- [ ] `Benchmark 记录表`
  - 状态：TODO

### P2

- [ ] `更完整的 profiling 面板`
  - 状态：DEFERRED
- [ ] `RenderDoc/Nsight 捕帧流程文档`
  - 状态：TODO

### 阶段验收标准

- [ ] resize 不崩溃
- [ ] 场景可以稳定加载与渲染
- [ ] CPU/GPU 渲染模式可切换
- [ ] 关键性能指标可展示
- [ ] 有可投递 demo 视频

### 阶段输出

- 技术总结文章
- demo 视频
- 性能基线表

---

## 5. 阶段 2：工具层与命令系统

### 阶段目标

- 让引擎具备可查询、可操作、可回滚、可审计的工具能力

### P0

- [ ] `EngineCommandBus / EditorFacade`
  - 说明：建立统一命令入口
  - 状态：TODO
- [ ] `Command Result 结构`
  - 说明：统一 success / error / payload 返回
  - 状态：TODO
- [ ] `Engine Context Snapshot`
  - 说明：生成可供 UI 和 AI 使用的结构化状态
  - 状态：TODO
- [ ] `Command Log Panel`
  - 状态：TODO
- [ ] `核心命令接口第一批`
  - 说明：`load_scene`、`set_camera`、`toggle_gpu_driven`、`get_engine_state`、`get_scene_summary`
  - 状态：TODO

### P1

- [ ] `list_materials / list_renderables`
  - 状态：TODO
- [ ] `材质编辑命令`
  - 状态：TODO
- [ ] `截图接口`
  - 状态：TODO
- [ ] `错误码体系`
  - 状态：TODO

### P2

- [ ] `Undo / Redo 雏形`
  - 状态：TODO
- [ ] `命令权限等级`
  - 状态：TODO

### 阶段验收标准

- [ ] 只通过命令接口即可完成常见引擎操作
- [ ] 至少 3 个只读命令稳定可用
- [ ] 至少 3 类状态可结构化输出
- [ ] 命令执行结果可记录
- [ ] 至少部分命令支持回滚

### 阶段输出

- 工具层架构图
- 命令系统文档
- 工具层演示视频

---

## 6. 阶段 3：AI Tool Use v1

### 阶段目标

- 把 LLM 与引擎命令系统连接起来，形成自然语言到工具调用的闭环

### P0

- [ ] `选择主模型供应商`
  - 状态：TODO
- [ ] `Aero Tool Schema`
  - 说明：定义工具 schema 和调用规范
  - 状态：TODO
- [ ] `LLM Orchestrator`
  - 说明：负责消息、工具调用、结果回填
  - 状态：TODO
- [ ] `AI 面板`
  - 说明：展示对话、工具调用和执行结果
  - 状态：TODO
- [ ] `只读任务链路`
  - 说明：先跑通场景查询、状态解释、资源统计
  - 状态：TODO

### P1

- [ ] `安全编辑任务链路`
  - 说明：相机控制、模式切换、材质调整
  - 状态：TODO
- [ ] `确认机制`
  - 状态：TODO
- [ ] `失败恢复`
  - 状态：TODO
- [ ] `Prompt / Context 优化`
  - 状态：TODO

### P2

- [ ] `多供应商抽象`
  - 状态：DEFERRED
- [ ] `会话持久化`
  - 状态：TODO

### 阶段验收标准

- [ ] 5 类只读任务可演示
- [ ] 3 类安全编辑任务可演示
- [ ] Tool Call 流程可视化
- [ ] 错误调用不会破坏引擎状态
- [ ] 有完整 3 到 5 分钟 demo

### 阶段输出

- AI Tool Use demo
- Tool Schema 文档
- 阶段总结文章

---

## 7. 阶段 4：语义层与 AI Native 雏形

### 阶段目标

- 让 AI 不只是调用命令，而是理解引擎内部语义状态

### P0

- [ ] `Engine Semantic Context`
  - 说明：统一抽象 Scene / Material / Resource / Pipeline 状态
  - 状态：TODO
- [ ] `Scene Summary`
  - 状态：TODO
- [ ] `Resource Health Summary`
  - 状态：TODO
- [ ] `Render State Summary`
  - 状态：TODO
- [ ] `诊断型工具第一批`
  - 说明：缺失贴图、场景不可见、渲染模式差异
  - 状态：TODO

### P1

- [ ] `before / after diff`
  - 状态：TODO
- [ ] `截图 + 状态绑定`
  - 状态：TODO
- [ ] `AI 诊断报告生成`
  - 状态：TODO

### P2

- [ ] `更细的 Material/Pass 语义建模`
  - 状态：DEFERRED
- [ ] `多模态上下文增强`
  - 状态：TODO

### 阶段验收标准

- [ ] AI 能基于语义摘要回答问题
- [ ] 至少 3 类诊断任务可运行
- [ ] 操作前后可输出差异总结
- [ ] 语义层结构可独立文档化

### 阶段输出

- AI Native 雏形设计文档
- 语义层架构图
- AI 诊断演示

---

## 8. 阶段 5：混合推理与运行时专题

### 阶段目标

- 把本地 GPU、云端模型和远程算力纳入统一路线，但不破坏主系统可交付性

### P0

- [ ] `混合推理架构`
  - 状态：TODO
- [ ] `本地小模型切入点选择`
  - 说明：意图分类、命令校验、上下文压缩等
  - 状态：TODO
- [ ] `云端/本地路由策略`
  - 状态：TODO
- [ ] `主系统降级模式`
  - 状态：TODO

### P1

- [ ] `Benchmark 实验`
  - 状态：TODO
- [ ] `远程算力离线流程`
  - 说明：评测、微调、embedding、数据生成
  - 状态：TODO
- [ ] `运行时专题方向确定`
  - 状态：TODO

### P2

- [ ] `NPC/运行时 agent`
  - 状态：DEFERRED
- [ ] `自然语言辅助场景构建`
  - 状态：DEFERRED
- [ ] `视觉分析专题`
  - 状态：DEFERRED

### 阶段验收标准

- [ ] 至少 1 个本地模型真正落地
- [ ] 主系统不依赖远程资源仍可运行
- [ ] 有一份量化实验结果
- [ ] 有一个运行时专题开始形成闭环

### 阶段输出

- 混合推理设计文档
- 实验报告
- 专题 demo

---

## 9. 风险清单

### 技术风险

- `渲染主干未稳定前过早进入 AI 集成`
- `命令系统未抽象完就直接做 tool use`
- `SceneRenderer / AssetManager 职责持续混乱`
- `上下文设计过于底层，导致模型不好用`
- `本地模型路线过早投入，拖慢主系统进度`

### 项目风险

- `目标太大，阶段成果不明确`
- `功能堆叠过多，演示质量不足`
- `文档、视频和 benchmark 缺失，导致成果无法展示`
- `长期项目中断后，缺少可独立成立的里程碑`

### 资源风险

- `远程超算不可长期依赖`
- `4060/4070S 显存限制影响本地大模型方案`
- `多方向同时推进导致精力分散`

---

## 10. 里程碑检查点

### Checkpoint A

- 时间：3 个月
- 目标：图形主干可投递
- 判断：
  - 是否已经有稳定 demo？
  - 是否已经能写出 3 条强简历 bullet？

### Checkpoint B

- 时间：6 个月
- 目标：工具层可展示
- 判断：
  - 是否已经有命令系统和状态快照？
  - 是否已经能把项目从“图形 demo”讲成“引擎工具平台”？

### Checkpoint C

- 时间：9 个月
- 目标：AI Tool Use 可演示
- 判断：
  - 是否已经形成自然语言到引擎命令的完整闭环？
  - 是否已经有 AI demo 视频？

### Checkpoint D

- 时间：12 个月
- 目标：AI Native 雏形
- 判断：
  - 是否已经有引擎语义层？
  - 是否已经能做 AI-assisted diagnostics？

---

## 11. 每周执行建议

每周建议只保留以下结构：

- 1 个主任务
- 1 个辅助任务
- 1 个文档或展示任务

示例：

- 主任务：完成 `Swapchain Recreate`
- 辅助任务：补 Scene 统计面板
- 文档任务：记录 resize 重建流程和踩坑

---

## 12. 项目管理规则

- 不在同一阶段同时开两个大方向
- 新功能只有在可展示时才算真正完成
- 每个阶段结束必须至少产出 1 篇文档和 1 个视频
- 每月必须更新 benchmark
- 每季度必须重新审视阶段目标，必要时砍掉 P2

---

## 13. 结论

项目管理上的核心原则只有一句话：

> 先保证 P0 能形成真正的阶段成果，再做 P1，最后才做 P2。

对于 `AeroEngine` 来说：

- P0 决定你能不能拿它找工作
- P1 决定你有没有明显辨识度
- P2 决定它能不能成为你长期的代表作
