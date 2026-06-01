#pragma once
#include <string>
#include <chrono>

namespace Aero {

	// 统一命令返回结构 — 所有命令通过此格式返回结果
	// 人类（ImGui）读 message，AI（工具调用）读 payload（JSON）
	struct CommandResult {
		bool success = false;
		std::string commandName; // 命令名，用于日志展示
		std::string message; // 人类可读的成功/失败描述
		std::string payload; // 结构化数据（JSON），供 AI 消费
		std::chrono::duration<double, std::milli> elapsed; // 执行耗时

		// 快捷工厂：创建成功结果
		static CommandResult ok(std::string cmdName, std::string msg, std::string jsonPayload = "{}") {
			return {true, std::move(cmdName), std::move(msg), std::move(jsonPayload), {}};
		}

		// 快捷工厂：创建失败结果
		static CommandResult fail(std::string cmdName, std::string msg, std::string jsonPayload = "{}") {
			return {false, std::move(cmdName), std::move(msg), std::move(jsonPayload), {}};
		}
	};

} // namespace Aero
