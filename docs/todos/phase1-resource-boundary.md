# Phase 1 P1: 资源生命周期清理 — TODO 跟踪清单

> 详细设计参考：`docs/phase1-resource-boundary.md`
> 对应 project-management.md: 阶段 1 → P1 → 资源生命周期清理

## 当前状态

- 开始日期：2026-05-27
- 整体状态：🔄 DOING
- 第一批、第二批已完成（AssetManager 核心方法全部实现）

---

## 第一批（数据结构 + AssetManager 上传核心）

| # | 文件 | 任务 | 难度 | 状态 |
|---|------|------|------|------|
| 1 | `asset_manager.cpp` | `upload_scene()` — 将 SceneData 上传为 GpuScene（顶点/索引/材质/纹理） | ⭐⭐ | ✅ DONE |
| 2 | `asset_manager.cpp` | `load_scene()` — 全流程：GLTFLoader::load_gltf → upload_scene → 缓存 | ⭐⭐ | ✅ DONE |
| 3 | `asset_manager.cpp` | `get_scene()` — 返回 GpuScene* 非持有指针 | ⭐ | ✅ DONE |

## 第二批（AssetManager 资源管理）

| # | 文件 | 任务 | 难度 | 状态 |
|---|------|------|------|------|
| 4 | `asset_manager.cpp` | `unload_scene()` — 销毁 GPU 资源并从注册表移除 | ⭐⭐ | ✅ DONE |
| 5 | `asset_manager.cpp` | `cleanup()` — 遍历 _loadedScenes 逐一销毁后再 clear | ⭐⭐ | ✅ DONE |

## 第三批（SceneRenderer 职责收窄）

| # | 文件 | 任务 | 难度 | 状态 |
|---|------|------|------|------|
| 6 | `SceneRenderer.h` | 新增 `bind_scene(const GpuScene&)` 声明 | ⭐⭐ | ⬜ TODO |
| 7 | `SceneRenderer.cpp` | 实现 `bind_scene()` — 生成 InstanceData + Indirect + 更新描述符 | ⭐⭐⭐ | ⬜ TODO |
| 8 | `SceneRenderer.h/.cpp` | 成员变量变更 — 移除旧 GPU 资源成员，新增 `_currentScene` 指针 | ⭐⭐ | ⬜ TODO |

## 第四批（上层连接 + 热重载适配）

| # | 文件 | 任务 | 难度 | 状态 |
|---|------|------|------|------|
| 9 | `SceneRenderer.cpp` | `draw()` — 从 `_currentScene` 读取 buffer 而非旧成员变量 | ⭐⭐ | ⬜ TODO |
| 10 | `SceneRenderer.cpp` | `cleanup()` — 只销毁 Instance/Indirect buffer，不销毁场景资源 | ⭐⭐ | ⬜ TODO |
| 11 | `aero_engine.cpp` | `init()` — 改用 AssetManager::load_scene + SceneRenderer::bind_scene 新流程 | ⭐⭐ | ⬜ TODO |
| 12 | `aero_engine.cpp` | `process_reload_requests()` — 热重载适配新架构 | ⭐⭐ | ⬜ TODO |
| 13 | `SceneRenderer.cpp` | 修改 submit/reload/reload_shaders 签名（GpuScene& 替代 SceneData&） | ⭐⭐ | ⬜ TODO |

## 第五批（清理与反思）

| # | 文件 | 任务 | 难度 | 状态 |
|---|------|------|------|------|
| 14 | `SceneRenderer.cpp` | 删除旧 `upload_scene()` 实现（已搬迁到 AssetManager） | ⭐ | ⬜ TODO |
| 15 | — | 思考题：submit_scene/reload_scene 应该属于 SceneRenderer 还是 AeroEngine？ | ⭐ | ⬜ TODO |

---

## 完成标准

- [ ] AssetManager 是场景 GPU 数据的唯一持有者
- [ ] SceneRenderer 通过 const GpuScene* 借用数据，不持有场景资源所有权
- [ ] 热重载正常工作
- [ ] GPU-driven / CPU-driven 两种路径都能正常渲染
- [ ] SceneRenderer::cleanup() 不再销毁场景纹理和 mesh buffer
