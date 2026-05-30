#pragma once
#include "RHI/vk_types.h"
#include <string>
#include <optional>

class GLTFLoader
{
public:
	static std::optional<SceneData> load_gltf(const std::string& filePath);

private:

};
