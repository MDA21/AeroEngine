#include "gltf_loader.h"
#include <iostream>

#define CGLTF_IMPLEMENTATION
#include "../external/cgltf/cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <filesystem>

std::optional<SceneData> GLTFLoader::load_gltf(const std::string& filePath) {
	cgltf_options options{};
	cgltf_data* data = nullptr;
	cgltf_result result = cgltf_parse_file(&options, filePath.c_str(), &data);

	if (result != cgltf_result_success) {
		std::cerr << "[AeroEngine] Failed to load glTF:" << filePath << std::endl;
		return std::nullopt;
	}

	cgltf_load_buffers(&options, data, filePath.c_str());

	SceneData scene;
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& cgltfMat = data->materials[i];
        MaterialParams mat = {};
        mat.albedoTexIdx = -1;
        mat.normalTexIdx = -1;
        mat.pbrTexIdx = -1;
        mat.emissiveTexIdx = -1;

        if (cgltfMat.has_pbr_metallic_roughness) {
            mat.baseColorFactor = glm::vec4(
                cgltfMat.pbr_metallic_roughness.base_color_factor[0],
                cgltfMat.pbr_metallic_roughness.base_color_factor[1],
                cgltfMat.pbr_metallic_roughness.base_color_factor[2],
                cgltfMat.pbr_metallic_roughness.base_color_factor[3]
            );

            mat.pbrFactors = glm::vec4(
                cgltfMat.pbr_metallic_roughness.roughness_factor,
                cgltfMat.pbr_metallic_roughness.metallic_factor,
                0.0f,
                0.0f
            );

            // Base Color (Albedo)
            if (cgltfMat.pbr_metallic_roughness.base_color_texture.texture && cgltfMat.pbr_metallic_roughness.base_color_texture.texture->image) {
                mat.albedoTexIdx = static_cast<int32_t>(cgltfMat.pbr_metallic_roughness.base_color_texture.texture->image - data->images);
            }

            // Metallic Roughness (PBR)
            if (cgltfMat.pbr_metallic_roughness.metallic_roughness_texture.texture && cgltfMat.pbr_metallic_roughness.metallic_roughness_texture.texture->image) {
                mat.pbrTexIdx = static_cast<int32_t>(cgltfMat.pbr_metallic_roughness.metallic_roughness_texture.texture->image - data->images);
            }
        }
        else {
            mat.baseColorFactor = glm::vec4(1.0f);
            mat.pbrFactors = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }

        // Normal
        if (cgltfMat.normal_texture.texture && cgltfMat.normal_texture.texture->image) {
            mat.normalTexIdx = static_cast<int32_t>(cgltfMat.normal_texture.texture->image - data->images);
        }

        // Emissive
        if (cgltfMat.emissive_texture.texture && cgltfMat.emissive_texture.texture->image) {
            mat.emissiveTexIdx = static_cast<int32_t>(cgltfMat.emissive_texture.texture->image - data->images);
        }

        scene.materials.push_back(mat);
    }

	if (scene.materials.empty()) {
		MaterialParams defaultMat = {};
		defaultMat.albedoTexIdx = -1;
		scene.materials.push_back(defaultMat);
	}
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        const cgltf_mesh& mesh = data->meshes[i];

        for (cgltf_size j = 0; j < mesh.primitives_count; ++j) {
            const cgltf_primitive& primitive = mesh.primitives[j];

            SubMesh subMesh = {};
            subMesh.firstIndex = static_cast<uint32_t>(scene.indices.size());
            subMesh.vertexOffset = static_cast<int32_t>(scene.vertices.size());
            subMesh.materialIndex = primitive.material ?
                static_cast<uint32_t>(std::distance(data->materials, primitive.material)) : 0;

            // 提取索引
            if (primitive.indices) {
                subMesh.indexCount = static_cast<uint32_t>(primitive.indices->count);
                for (cgltf_size k = 0; k < primitive.indices->count; ++k) {
                    scene.indices.push_back(static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, k)));
                }
            }
            else {
                continue; // 暂不支持无索引绘制
            }

            size_t vertexCount = 0;
            for (cgltf_size k = 0; k < primitive.attributes_count; ++k) {
                if (primitive.attributes[k].type == cgltf_attribute_type_position) {
                    vertexCount = primitive.attributes[k].data->count;
                    break;
                }
            }

            size_t currentVertexStart = scene.vertices.size();
            scene.vertices.resize(currentVertexStart + vertexCount);

            glm::vec3 aabbMin = glm::vec3(std::numeric_limits<float>::max());
            glm::vec3 aabbMax = glm::vec3(std::numeric_limits<float>::lowest());

            for (cgltf_size k = 0; k < primitive.attributes_count; ++k) {
                const cgltf_attribute& attribute = primitive.attributes[k];

                for (cgltf_size v = 0; v < attribute.data->count; ++v) {
                    Vertex& vertex = scene.vertices[currentVertexStart + v];
                    float values[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    cgltf_accessor_read_float(attribute.data, v, values, 4);

                    if (attribute.type == cgltf_attribute_type_position) {
                        vertex.position = { values[0], values[1], values[2] };
                        aabbMin = glm::min(aabbMin, vertex.position);
                        aabbMax = glm::max(aabbMax, vertex.position);
                    }
                    else if (attribute.type == cgltf_attribute_type_normal) {
                        vertex.normal = { values[0], values[1], values[2] };
                    }
                    else if (attribute.type == cgltf_attribute_type_texcoord) {
                        vertex.uv_x = values[0];
                        vertex.uv_y = values[1];
                    }
                    else if (attribute.type == cgltf_attribute_type_tangent) {
                        vertex.tangent = { values[0], values[1], values[2], values[3] };
                    }
                }
            }
            subMesh.aabbMin = aabbMin;
            subMesh.aabbMax = aabbMax;
            scene.subMeshes.push_back(subMesh);
        }
    }

    std::filesystem::path basePath = std::filesystem::path(filePath).parent_path();

    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image& cgltfImage = data->images[i];
        LoadedImage img = {};

        // 强制要求 stb_image 加载出 4 通道 (RGBA)，为了 GPU 内存对齐
        int desiredChannels = 4;

        if (cgltfImage.buffer_view) {
            // 情况 A: 图片被打包进了二进制 Buffer (如 .glb)
            unsigned char* bufferData = (unsigned char*)cgltfImage.buffer_view->buffer->data + cgltfImage.buffer_view->offset;
            size_t bufferSize = cgltfImage.buffer_view->size;

            img.pixels = stbi_load_from_memory(bufferData, (int)bufferSize, &img.width, &img.height, &img.channels, desiredChannels);
        }
        else if (cgltfImage.uri) {
            // 情况 B: 图片是外部文件，需要拼接绝对路径
            std::string imagePath = (basePath / cgltfImage.uri).string();
            img.pixels = stbi_load(imagePath.c_str(), &img.width, &img.height, &img.channels, desiredChannels);
        }

        if (!img.pixels) {
            std::cerr << "[GLTFLoader] Failed to load image: " << (cgltfImage.uri ? cgltfImage.uri : "Embedded Buffer") << std::endl;
        }

        scene.images.push_back(img);
    }

    cgltf_free(data);
    return scene;
}