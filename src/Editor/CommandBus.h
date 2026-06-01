#pragma once
#include "Editor/CommandResult.h"
#include <string>
#include <vector>

// 前置声明 — 避免重头文件依赖
namespace Aero {
	namespace Renderer {
		class RenderContext;
	} // namespace Renderer
} // namespace Aero

class Camera;
namespace Aero {
	namespace Resource {
		class AssetManager;
	} // namespace Resource
} // namespace Aero

namespace Aero {

	// ---- 命令日志条目 ----

	struct CommandLogEntry {
		std::string commandName;
		bool success;
		std::string message;
		double elapsedMs;
		// TODO #? (Batch 3): 添加时间戳 (std::chrono::system_clock::time_point)
	};

	// ---- 命令基类 ----

	class ICommand {
	public:
		virtual ~ICommand() = default;

		// 执行命令，返回统一结果
		// 子类在 execute() 中访问所需的引擎子系统指针
		virtual CommandResult execute() = 0;
	};

	// ---- 首批 P0 命令 ----
	// 设计：每个命令类持有所需的引擎子系统指针，
	// 由 AeroEngine 在创建命令时注入，CommandBus 只负责执行 + 日志

	class CmdGetSceneSummary : public ICommand {
	public:
		// 注入依赖：RenderContext 提供场景统计接口
		Aero::Renderer::RenderContext* renderContext = nullptr;

		CommandResult execute() override;
	};

	class CmdToggleGPUDriven : public ICommand {
	public:
		// 注入依赖：指向 AeroEngine::_useGPUDriven 的指针
		bool* gpuDriven = nullptr;

		CommandResult execute() override;
	};

	class CmdSetCamera : public ICommand {
	public:
		// 注入依赖：指向 AeroEngine::_camera 的指针
		Camera* camera = nullptr;

		// 命令参数：目标相机状态
		float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
		float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;

		CommandResult execute() override;
	};

	class CmdGetEngineState : public ICommand {
	public:
		// 注入依赖：聚合多个子系统信息
		Aero::Renderer::RenderContext* renderContext = nullptr;
		Camera* camera = nullptr;
		bool* gpuDriven = nullptr;
		std::string* scenePath = nullptr; // 指向 AeroEngine::_currentScenePath
		float* displayFps = nullptr; // 指向 AeroEngine::_displayPerfStats.displayFps

		CommandResult execute() override;
	};

	class CmdLoadScene : public ICommand {
	public:
		// 注入依赖：需要 AssetManager 加载 + RenderContext 提交
		Aero::Renderer::RenderContext* renderContext = nullptr;
		Aero::Resource::AssetManager* assetManager = nullptr;

		// 命令参数
		std::string scenePath;
		std::string sceneKey = "main";

		CommandResult execute() override;
	};

	// ---- 命令总线 ----

	class CommandBus {
	public:
		static constexpr size_t kMaxLogEntries = 200;

		// 执行任意命令：计时 → execute() → 记录日志 → 返回结果
		CommandResult execute(ICommand& cmd);

		// 获取最近 N 条日志（默认返回全部）
		std::vector<CommandLogEntry> get_log(size_t count = kMaxLogEntries) const;

		// 清空日志
		void clear_log();

	private:
		void append_log(const CommandResult& result);

		// 环形缓冲区：固定容量，满了覆盖最旧的条目
		std::vector<CommandLogEntry> _log;
		size_t _writeIndex = 0;
	};

} // namespace Aero
