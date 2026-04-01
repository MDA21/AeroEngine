#include "gltf_loader.h"
#include <iostream>

#define CGLTF_IMPLEMENTATION
#include "../external/cgltf/cgltf.h"

std::optional<SceneData> GLTFLoader::load_gltf(const std::string& filePath) {
	cgltf_options options{};
	cgltf_data* data = nullptr;
	cgltf_result result = cgltf_parse_file(&options, filePath.c_str(), &data);

	if (result != cgltf_result_success) {
		std::cerr << "[AeroEngine] Failed to load glTF:" << filePath << std::endl;
		return std::nullopt;
	}
}