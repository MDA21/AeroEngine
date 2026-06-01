# Phase 2 — 命令系统 TODO 跟踪

> 对应 `docs/project-management.md` Phase 2 P0 任务
> 状态: 🔴 IN PROGRESS (2026-06-01)

## 架构图

```
AeroEngine → RenderContext ─┬→ SceneRenderer (纯渲染执行)
                             ├→ CommandBus (命令队列 + 日志)
                             └→ StateSnapshot (可查询的引擎状态)
```

## 全量 TODO 列表

### Batch 1: 核心命令系统 ✅ DONE

| # | 文件 | 函数/位置 | 任务 | 难度 | 状态 |
|---|------|-----------|------|------|------|
| 1 | `Editor/CommandBus.cpp` | `CommandBus::execute()` | 实现命令执行计时 + 日志记录（环形缓冲区） | ⭐⭐ | ✅ DONE |
| 2 | `Editor/CommandBus.cpp` | `CmdGetSceneSummary::execute()` | 调用 RenderContext::get_scene_stats() 格式化为 JSON | ⭐⭐ | ✅ DONE |
| 3 | `Editor/CommandBus.cpp` | `CmdToggleGPUDriven::execute()` | 切换 gpuDriven bool 标志引用 | ⭐ | ✅ DONE |
| 4 | `Editor/CommandBus.cpp` | `CmdSetCamera::execute()` | 设置相机 Position / Front / 视角参数 | ⭐⭐ | ✅ DONE |

### Batch 2: 剩余命令 + 引擎集成 (4 TODOs)

| # | 文件 | 函数/位置 | 任务 | 难度 | 状态 |
|---|------|-----------|------|------|------|
| 5 | `CommandBus.cpp` | `CmdGetEngineState::execute()` | 聚合多信息源生成引擎状态快照 JSON | ⭐⭐⭐ | TODO |
| 6 | `CommandBus.cpp` | `CmdLoadScene::execute()` | 调用 AssetManager 加载场景 + 提交到 RenderContext | ⭐⭐⭐⭐ | TODO |
| 7 | `aero_engine.cpp` | `AeroEngine::init()` | 初始化 CommandBus 实例 | ⭐ | TODO |
| 8 | `aero_engine.cpp` | `AeroEngine::draw()` | 添加 ImGui Command Log 面板（日志列表 + 测试按钮 + Clear） | ⭐⭐⭐ | TODO |

### Batch 3: CMake + 编译验证

| # | 文件 | 函数/位置 | 任务 | 难度 | 状态 |
|---|------|-----------|------|------|------|
| 9 | `CMakeLists.txt` | 源文件列表 | 添加 `src/Core/CommandBus.cpp` 到 add_executable | ⭐ | TODO |

## TODO 依赖关系

```
TODO #1~#4 ✅ → TODO #9 (CMake) → 编译验证
              → TODO #5 → TODO #6 → TODO #7 → TODO #8 (下次)
```

## 学习路线

```
Batch 1: TODO #1 → #2 → #3 → #4  ✅ DONE (2026-06-01)
Batch 2: TODO #5 → #6 → #7 → #8  (下次)
Batch 3: TODO #9                  (编译前)
```

## 验收标准 (Phase 2 P0)

- [ ] 所有命令有统一 CommandResult 返回
- [ ] 命令执行结果可记录、可审计
- [ ] Command Log 面板显示最近 20 条命令
- [ ] `get_scene_summary` 返回正确场景统计
- [ ] `toggle_gpu_driven` 能切换渲染模式
- [ ] 编译通过，引擎正常运行不作
