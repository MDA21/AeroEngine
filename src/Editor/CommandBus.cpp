#include "Editor/CommandBus.h"
#include "Renderer/RenderContext.h"
#include "Core/camera.h"
#include "Resource/asset_manager.h"
#include <chrono>
#include <sstream>
#include <glm/glm.hpp>
#include <iomanip>

namespace Aero {

	// ============================================================
	// CommandBus 实现
	// ============================================================

	CommandResult CommandBus::execute(ICommand& cmd) {
		auto start = std::chrono::high_resolution_clock::now();
		CommandResult result = cmd.execute();
		auto end = std::chrono::high_resolution_clock::now();
		result.elapsed = end - start;

		append_log(result);

		return result;
	}

	void CommandBus::append_log(const CommandResult& result) {
		CommandLogEntry entry;
		entry.commandName = result.commandName;
		entry.success = result.success;
		entry.message = result.message;
		entry.elapsedMs = result.elapsed.count();

		if (_log.size() < kMaxLogEntries) {
			_log.push_back(std::move(entry));
		} else {
			_log[_writeIndex] = std::move(entry);
		}
		_writeIndex = (_writeIndex + 1) % kMaxLogEntries;
	}

	std::vector<CommandLogEntry> CommandBus::get_log(size_t count) const {
		if (_log.empty())
			return {};

		size_t actual = (count < _log.size()) ? count : _log.size();
		std::vector<CommandLogEntry> result;
		result.reserve(actual);

		if (_log.size() < kMaxLogEntries) {
			for (size_t i = _log.size() - actual; i < _log.size(); ++i) {
				result.push_back(_log[i]);
			}
		} else {
			size_t start = (_writeIndex + kMaxLogEntries - actual) % kMaxLogEntries;
			for (size_t i = 0; i < actual; ++i) {
				result.push_back(_log[(start + i) % kMaxLogEntries]);
			}
		}
		return result;
	}

	void CommandBus::clear_log() {
		_log.clear();
		_writeIndex = 0;
	}

	// ============================================================
	// CmdGetSceneSummary — 获取场景统计摘要
	// ============================================================

	CommandResult CmdGetSceneSummary::execute() {
		if (!renderContext) {
			return CommandResult::fail("CmdGetSceneSummary", "renderContext is null");
		}

		const SceneStats& stats = renderContext->get_scene_stats();
		std::ostringstream oss;
		oss << "{"
		    << "\"meshes\": " << stats.meshCount << ", "
		    << "\"submeshes\": " << stats.submeshCount << ", "
		    << "\"materials\": " << stats.materialCount << ", "
		    << "\"textures\": " << stats.textureCount << ", "
		    << "\"vertices\": " << stats.vertexCount << ", "
		    << "\"indices\": " << stats.indexCount
		    << "}";

		// 人类可读摘要放在 message，结构化 JSON 放在 payload
		std::ostringstream summary;
		summary << stats.meshCount << " meshes, "
		        << stats.submeshCount << " submeshes, "
		        << stats.materialCount << " materials, "
		        << stats.textureCount << " textures";

		return CommandResult::ok("CmdGetSceneSummary", summary.str(), oss.str());
	}

	// ============================================================
	// CmdToggleGPUDriven — 切换 GPU/CPU 渲染模式
	// ============================================================

	CommandResult CmdToggleGPUDriven::execute() {
		if (!gpuDriven) {
			return CommandResult::fail("CmdToggleGPUDriven", "gpuDriven pointer is null");
		}

		bool oldValue = *gpuDriven;
		*gpuDriven = !(*gpuDriven);

		std::string newState = (*gpuDriven) ? "GPU-Driven" : "CPU-Driven";
		std::string oldState = oldValue ? "GPU-Driven" : "CPU-Driven";
		std::string message = "Toggled rendering mode from " + oldState + " to " + newState;

		std::ostringstream payload;
		payload << R"({"gpuDriven": )" << (*gpuDriven ? "true" : "false") << "}";

		return CommandResult::ok("CmdToggleGPUDriven", message, payload.str());
	}

	// ============================================================
	// CmdSetCamera — 设置相机位置和朝向
	// ============================================================

	CommandResult CmdSetCamera::execute() {
		if (!camera) {
			return CommandResult::fail("CmdSetCamera", "camera pointer is null");
		}

		glm::vec3 position(posX, posY, posZ);
		glm::vec3 target(targetX, targetY, targetZ);

		// 检查 position == target 导致零向量的边界情况
		if (glm::distance(position, target) < 0.0001f) {
			return CommandResult::fail("CmdSetCamera", "Camera position and target are too close");
		}

		camera->Position = position;
		glm::vec3 front = glm::normalize(target - position);
		camera->Front = front;
		camera->Right = glm::normalize(glm::cross(front, camera->WorldUp));
		camera->Up = glm::normalize(glm::cross(camera->Right, front));

		// 从 Front 向量反算 Euler 角（用于 ImGui 面板显示）
		float pitch = glm::degrees(asin(-front.y));
		float yaw = glm::degrees(atan2(front.x, front.z));

		std::ostringstream payload;
		payload << R"({"position": [)" << posX << ", " << posY << ", " << posZ << "], "
		        << R"("target": [)" << targetX << ", " << targetY << ", " << targetZ << "], "
		        << R"("yaw": )" << yaw << ", "
		        << R"("pitch": )" << pitch << "}";

		return CommandResult::ok("CmdSetCamera",
		                         "Camera set to (" + std::to_string(posX) + ", " + std::to_string(posY) + ", " + std::to_string(posZ) + ")",
		                         payload.str());
	}

	// ============================================================
	// CmdGetEngineState — 引擎状态快照（Batch 2 实现）
	// ============================================================

	CommandResult CmdGetEngineState::execute() {
		if (!renderContext || !camera || !gpuDriven || !scenePath || !displayFps) {
			return CommandResult::fail("CmdGetEngineState", "One or more required pointers are null");
		}

		const SceneStats& stats = renderContext->get_scene_stats();

		std::ostringstream payload;
		payload << R"({"scene_path": ")" << *scenePath << R"(", )"
		        << R"("gpu_driven": )" << (*gpuDriven ? "true" : "false") << R"(, )"
		        << R"("fps": )" << *displayFps << R"(, )"
		        << R"("camera_position": [)" << std::fixed << std::setprecision(1)
		        << camera->Position.x << ", " << camera->Position.y << ", " << camera->Position.z << R"(], )"
		        << R"("scene_stats": )" << "{"
		        << "\"meshes\": " << stats.meshCount << ", "
		        << "\"submeshes\": " << stats.submeshCount << ", "
		        << "\"materials\": " << stats.materialCount << ", "
		        << "\"textures\": " << stats.textureCount << ", "
		        << "\"vertices\": " << stats.vertexCount << ", "
		        << "\"indices\": " << stats.indexCount
		        << "}" << "}";

		return CommandResult::ok("CmdGetEngineState", "Engine state snapshot", payload.str());
	}

	// ============================================================
	// CmdLoadScene — 加载并切换场景（Batch 2 实现）
	// ============================================================

	CommandResult CmdLoadScene::execute() {
		if (!assetManager || !renderContext) {
			return CommandResult::fail("CmdLoadScene", "assetManager or renderContext pointer is null");
		}
		if (scenePath.empty()) {
			return CommandResult::fail("CmdLoadScene", "scenePath is empty");
		}
		assetManager->unload_scene(sceneKey);
		if (!assetManager->load_scene(sceneKey, scenePath)) {
			return CommandResult::fail("CmdLoadScene", "Failed to load scene from path: " + scenePath);
		}
		const GpuScene* gpuScene = assetManager->get_scene(sceneKey);
		if (gpuScene == nullptr) {
			return CommandResult::fail("CmdLoadScene", "Failed to get GPU scene for key: " + sceneKey);
		}
		if (!renderContext->submit_scene(*gpuScene, nullptr)) {
			return CommandResult::fail("CmdLoadScene", "Failed to submit scene to render context");
		}

		return CommandResult::ok("CmdLoadScene", "Scene loaded successfully", R"({"scene_path": ")" + scenePath + R"(", "scene_key": ")" + sceneKey + R"("})");
	}

} // namespace Aero
