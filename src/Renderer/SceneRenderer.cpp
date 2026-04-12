#include "SceneRenderer.h"
#include "RHI/vk_initializers.h"
#include "RHI/vk_pipelines.h"
#include <stb_image.h>
#include <iostream>
#include <array>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include "Resource/asset_manager.h"

namespace Aero {
    namespace Renderer {

        struct ComputePushConstants {
            glm::vec4 planes[6];
            uint32_t instanceCount;
        };

        static std::array<glm::vec4, 6> get_frustum_planes(const glm::mat4& viewProj) {
            std::array<glm::vec4, 6> planes;

            // glm 是列主序，为了套用标准行主序提取公式，我们先求转置
            glm::mat4 M = glm::transpose(viewProj);

            planes[0] = M[3] + M[0]; // Left
            planes[1] = M[3] - M[0]; // Right
            planes[2] = M[3] + M[1]; // Bottom
            planes[3] = M[3] - M[1]; // Top
            planes[4] = M[2];        // Near (在 Reverse-Z 和 Vulkan ZO 下依然适用)
            planes[5] = M[3] - M[2]; // Far

            //归一化平面法线，这样 plane.w 的物理意义就是点到原点的距离
            for (auto& p : planes) {
                float length = glm::length(glm::vec3(p.x, p.y, p.z));
                p /= length;
            }
            return planes;
        }

        void SceneRenderer::init(Aero::RHI::VulkanDevice* device, uint32_t windowWidth, uint32_t windowHeight) {
            _renderDevice = device;

            // 默认采样器
            VkSamplerCreateInfo samplerInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
            VK_CHECK(vkCreateSampler(_renderDevice->get_device(), &samplerInfo, nullptr, &_defaultSamplerLinear));
            _deletionQueue.push_function([=]() { vkDestroySampler(_renderDevice->get_device(), _defaultSamplerLinear, nullptr); });

            init_bindless_descriptor();
            init_depth_image(windowWidth,windowHeight);
            init_pipelines();
        }

        void SceneRenderer::cleanup() {
            _deletionQueue.flush();
        }

        void SceneRenderer::draw(VkCommandBuffer cmd, VkImageView targetImageView, const Camera& camera, uint32_t screenWidth, uint32_t screenHeight, bool useGPUDriven) {
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 proj = glm::perspectiveZO(glm::radians(camera.Fov), (float)screenWidth / (float)screenHeight, 10000.0f, 0.1f);
            proj[1][1] *= -1;
            glm::mat4 viewProj = proj * view;

            // ================= 阶段 1：Compute Culling =================
            if (_instanceCount > 0 && useGPUDriven) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cullingPipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cullingPipelineLayout, 0, 1, &_globalDescriptorSet, 0, nullptr);

                ComputePushConstants computePush{};
                computePush.instanceCount = _instanceCount;
                auto planes = get_frustum_planes(viewProj);
                for (int i = 0; i < 6; i++) computePush.planes[i] = planes[i];

                vkCmdPushConstants(cmd, _cullingPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &computePush);

                uint32_t groupCount = (_instanceCount + 255) / 256;
                vkCmdDispatch(cmd, groupCount, 1, 1);

                VkBufferMemoryBarrier indirectBarrier{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
                indirectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                indirectBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                indirectBarrier.buffer = _drawIndirectBuffer.buffer;
                indirectBarrier.offset = 0;
                indirectBarrier.size = VK_WHOLE_SIZE;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, 1, &indirectBarrier, 0, nullptr);
            }

            // ================= 阶段 2：Graphics Rendering =================
            VkClearValue clearValue;
            clearValue.color = { {0.05f, 0.05f, 0.08f, 1.0f} };
            VkExtent2D currentExtent = { screenWidth, screenHeight };

            VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, &clearValue);
            VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_depthImage.view);
            VkRenderingInfo renderInfo = vkinit::rendering_info(currentExtent, &colorAttachment, &depthAttachment);

            vkCmdBeginRendering(cmd, &renderInfo);

            VkViewport viewport{ 0.0f, 0.0f, (float)screenWidth, (float)screenHeight, 0.0f, 1.0f };
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{ {0, 0}, currentExtent };
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);

            if (_instanceCount > 0 && _mainMeshBuffers.vertexBuffer.buffer != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipelineLayout, 0, 1, &_globalDescriptorSet, 0, nullptr);
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &_mainMeshBuffers.vertexBuffer.buffer, &offset);
                vkCmdBindIndexBuffer(cmd, _mainMeshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

                vkCmdPushConstants(cmd, _trianglePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4), &viewProj);

                if (useGPUDriven) {
                    vkCmdDrawIndexedIndirect(cmd, _drawIndirectBuffer.buffer, 0, _instanceCount, sizeof(VkDrawIndexedIndirectCommand));
                }
                else {
                    for (uint32_t i = 0; i < _instanceCount; i++) {
                        const SubMesh& submesh = _renderables[i % _renderables.size()];
                        vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.firstIndex, submesh.vertexOffset, i);
                    }
                }
            }
            vkCmdEndRendering(cmd);
        }

        void Aero::Renderer::SceneRenderer::init_pipelines() {
            VkDevice device = _renderDevice->get_device();
            VkFormat swapchainFormat = _renderDevice->get_swapchain_format();

            VkShaderModule triangleFragShader;
            if (!vkutil::load_shader_module("shaders/mesh.frag.spv", device, &triangleFragShader)) {
                std::cout << "Error when building the triangle fragment shader module" << std::endl;
            }
            VkShaderModule triangleVertShader;
            if (!vkutil::load_shader_module("shaders/mesh.vert.spv", device, &triangleVertShader)) {
                std::cout << "Error when building the triangle vertex shader module" << std::endl;
            }

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = sizeof(glm::mat4);
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            //创建 Pipeline Layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &_globalSetLayout;
            pipelineLayoutInfo.pushConstantRangeCount = 1;
            pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

            VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &_trianglePipelineLayout));

            //配置 Pipeline Builder
            PipelineBuilder builder;
            builder._pipelineLayout = _trianglePipelineLayout;

            //顶点着色器阶段
            VkPipelineShaderStageCreateInfo vertStage{};
            vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertStage.module = triangleVertShader;
            vertStage.pName = "main";
            builder._shaderStages.push_back(vertStage);

            //片段着色器阶段
            VkPipelineShaderStageCreateInfo fragStage{};
            fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragStage.module = triangleFragShader;
            fragStage.pName = "main";
            builder._shaderStages.push_back(fragStage);

            builder._inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            builder._inputAssembly.primitiveRestartEnable = VK_FALSE;

            auto bindingDescription = Vertex::getBindingDescription();
            auto attributeDescriptions = Vertex::getAttributeDescriptions();

            builder._vertexInputInfo = {};
            builder._vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            builder._vertexInputInfo.vertexBindingDescriptionCount = 1;
            builder._vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
            builder._vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
            builder._vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

            builder._depthStencil.depthTestEnable = VK_TRUE;
            builder._depthStencil.depthWriteEnable = VK_TRUE;
            builder._depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;

            builder._rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            builder._rasterizer.lineWidth = 1.0f;
            builder._rasterizer.cullMode = VK_CULL_MODE_NONE;
            builder._rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

            builder._multisampling.sampleShadingEnable = VK_FALSE;
            builder._multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            builder._colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            builder._colorBlendAttachment.blendEnable = VK_FALSE;

            builder._renderInfo.colorAttachmentCount = 1;
            builder._renderInfo.pColorAttachmentFormats = &swapchainFormat;
            builder._renderInfo.depthAttachmentFormat = _depthImageFormat;

            _trianglePipeline = builder.build_pipeline(device);

            vkDestroyShaderModule(device, triangleFragShader, nullptr);
            vkDestroyShaderModule(device, triangleVertShader, nullptr);

            _deletionQueue.push_function([=]() {
                vkDestroyPipeline(device, _trianglePipeline, nullptr);
                vkDestroyPipelineLayout(device, _trianglePipelineLayout, nullptr);
                });

            //compute culling pipeline
            VkShaderModule computeShader;
            if (!vkutil::load_shader_module("shaders/culling.comp.spv", device, &computeShader)) {
                std::cerr << "Error when building the culling compute shader module" << std::endl;
            }
            VkPushConstantRange computePushConstant{};
            computePushConstant.offset = 0;
            computePushConstant.size = sizeof(ComputePushConstants);
            computePushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkPipelineLayoutCreateInfo computeLayoutInfo{};
            computeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            computeLayoutInfo.setLayoutCount = 1;
            computeLayoutInfo.pSetLayouts = &_globalSetLayout;
            computeLayoutInfo.pushConstantRangeCount = 1;
            computeLayoutInfo.pPushConstantRanges = &computePushConstant;

            VK_CHECK(vkCreatePipelineLayout(device, &computeLayoutInfo, nullptr, &_cullingPipelineLayout));

            VkComputePipelineCreateInfo computePipelineInfo{};
            computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            computePipelineInfo.layout = _cullingPipelineLayout;
            computePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            computePipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            computePipelineInfo.stage.module = computeShader;
            computePipelineInfo.stage.pName = "main";

            VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &_cullingPipeline));

            vkDestroyShaderModule(_renderDevice->get_device(), computeShader, nullptr);

            _deletionQueue.push_function([=]() {
                vkDestroyPipeline(device, _cullingPipeline, nullptr);
                vkDestroyPipelineLayout(device, _cullingPipelineLayout, nullptr);
                });
        }

        void Aero::Renderer::SceneRenderer::init_depth_image(uint32_t width, uint32_t height) {
            VmaAllocator allocator = _renderDevice->get_allocator();

            VkExtent3D depthImageExtent = {
                width,
                height,
                1
            };

            VkImageCreateInfo dimg_info{};
            dimg_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            dimg_info.imageType = VK_IMAGE_TYPE_2D;
            dimg_info.format = _depthImageFormat;
            dimg_info.extent = depthImageExtent;
            dimg_info.mipLevels = 1;
            dimg_info.arrayLayers = 1;
            dimg_info.samples = VK_SAMPLE_COUNT_1_BIT;
            dimg_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            dimg_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

            VmaAllocationCreateInfo dimg_allocinfo{};
            dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            VK_CHECK(vmaCreateImage(allocator, &dimg_info, &dimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr));

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = _depthImage.image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = _depthImageFormat;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            VK_CHECK(vkCreateImageView(_renderDevice->get_device(), &view_info, nullptr, &_depthImage.view));

            _renderDevice->immediate_submit([&](VkCommandBuffer cmd) {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                barrier.image = _depthImage.image;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                });

            _deletionQueue.push_function([=]() {
                vkDestroyImageView(_renderDevice->get_device(), _depthImage.view, nullptr);
                vmaDestroyImage(allocator, _depthImage.image, _depthImage.allocation);
                });

            std::cout << "[AeroEngine] Depth Image allocated successfully." << std::endl;
        }

        void Aero::Renderer::SceneRenderer::init_bindless_descriptor() {
            VkDescriptorSetLayoutBinding materialBind{};
            materialBind.binding = 0;
            materialBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            materialBind.descriptorCount = 1;
            materialBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding textureBind{};
            textureBind.binding = 1;
            textureBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureBind.descriptorCount = 4096;
            textureBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding instanceBind{};
            instanceBind.binding = 2;
            instanceBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            instanceBind.descriptorCount = 1;
            instanceBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutBinding indirectBind{};
            indirectBind.binding = 3;
            indirectBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            indirectBind.descriptorCount = 1;
            indirectBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutBinding bindings[]{ materialBind, textureBind, instanceBind,indirectBind };

            VkDescriptorBindingFlags bindlessFlags =
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

            VkDescriptorBindingFlags bindingFlags[4] = { 0, bindlessFlags,0,0 };

            VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
            extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            extendedInfo.bindingCount = 4;
            extendedInfo.pBindingFlags = bindingFlags;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.pNext = &extendedInfo;
            layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            layoutInfo.bindingCount = 4;
            layoutInfo.pBindings = bindings;

            VK_CHECK(vkCreateDescriptorSetLayout(_renderDevice->get_device(), &layoutInfo, nullptr, &_globalSetLayout));

            VkDescriptorPoolSize poolSizes[] = {
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 }
            };

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = 2;
            poolInfo.pPoolSizes = poolSizes;

            VK_CHECK(vkCreateDescriptorPool(_renderDevice->get_device(), &poolInfo, nullptr, &_globalDescriptorPool));

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = _globalDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &_globalSetLayout;

            VK_CHECK(vkAllocateDescriptorSets(_renderDevice->get_device(), &allocInfo, &_globalDescriptorSet));

            _deletionQueue.push_function([=]() {
                vkDestroyDescriptorPool(_renderDevice->get_device(), _globalDescriptorPool, nullptr);
                vkDestroyDescriptorSetLayout(_renderDevice->get_device(), _globalSetLayout, nullptr);
                });

            std::cout << "[AeroEngine] Bindless Descriptor Setup Complete." << std::endl;
        }

        void Aero::Renderer::SceneRenderer::update_global_descriptor_set() {

            VkDescriptorBufferInfo matBufferInfo{};
            matBufferInfo.buffer = _materialBuffer.buffer;
            matBufferInfo.offset = 0;
            matBufferInfo.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet matWrite{};
            matWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            matWrite.dstSet = _globalDescriptorSet;
            matWrite.dstBinding = 0; //binding 1 for material SSBO
            matWrite.dstArrayElement = 0;
            matWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            matWrite.descriptorCount = 1;
            matWrite.pBufferInfo = &matBufferInfo;

            VkDescriptorBufferInfo instBufferInfo{};
            instBufferInfo.buffer = _instanceBuffer.buffer;
            instBufferInfo.offset = 0;
            instBufferInfo.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet instWrite{};
            instWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            instWrite.dstSet = _globalDescriptorSet;
            instWrite.dstBinding = 2; //binding 2 for instance SSBO;
            instWrite.dstArrayElement = 0;
            instWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            instWrite.descriptorCount = 1;
            instWrite.pBufferInfo = &instBufferInfo;

            VkDescriptorBufferInfo indirectBufferInfo{};
            indirectBufferInfo.buffer = _drawIndirectBuffer.buffer;
            indirectBufferInfo.offset = 0;
            indirectBufferInfo.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet indirectWrite{};
            indirectWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            indirectWrite.dstSet = _globalDescriptorSet;
            indirectWrite.dstBinding = 3;
            indirectWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            indirectWrite.descriptorCount = 1;
            indirectWrite.pBufferInfo = &indirectBufferInfo;

            // 修改：提交 3 个写入操作
            VkWriteDescriptorSet writes[] = { matWrite, instWrite, indirectWrite };
            vkUpdateDescriptorSets(_renderDevice->get_device(), 3, writes, 0, nullptr);
        }

        void Aero::Renderer::SceneRenderer::update_bindless_texture(const AllocatedImage& image, uint32_t textureID) {
            VkDescriptorImageInfo imageBufferInfo{};
            imageBufferInfo.sampler = _defaultSamplerLinear;
            imageBufferInfo.imageView = image.view;
            imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet textureWrite{};
            textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            textureWrite.dstSet = _globalDescriptorSet;
            textureWrite.dstBinding = 1; // 绑在 Binding 1 (globalTextures)
            textureWrite.dstArrayElement = textureID; // 关键！插到数组的哪个索引
            textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureWrite.descriptorCount = 1;
            textureWrite.pImageInfo = &imageBufferInfo;

            vkUpdateDescriptorSets(_renderDevice->get_device(), 1, &textureWrite, 0, nullptr);
        }

        void SceneRenderer::upload_scene(const SceneData& scene) {
            auto& assetManager = Aero::Resource::AssetManager::Get();

            std::cout << "[SceneRenderer] Start async packing & uploading scene..." << std::endl;

            size_t vertexBufferSize = scene.vertices.size() * sizeof(Vertex);
            _mainMeshBuffers.vertexBuffer = assetManager.upload_buffer_async(
                vertexBufferSize, scene.vertices.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

            size_t indexBufferSize = scene.indices.size() * sizeof(uint32_t);
            _mainMeshBuffers.indexBuffer = assetManager.upload_buffer_async(
                indexBufferSize, scene.indices.data(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

            size_t materialBufferSize = scene.materials.size() * sizeof(MaterialParams);
            if (materialBufferSize > 0) {
                _materialBuffer = assetManager.upload_buffer_async(
                    materialBufferSize, scene.materials.data(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            }

            _sceneTextures.clear();
            for (uint32_t i = 0; i < scene.images.size(); i++) {
                const auto& img = scene.images[i];
                size_t pixelSize = img.width * img.height * 4;

                AllocatedImage tex = assetManager.upload_image_async(
                    img.width, img.height, VK_FORMAT_R8G8B8A8_UNORM, img.pixels, pixelSize);

                _sceneTextures.push_back(tex);

                //把上传好的纹理当场插进 Bindless 描述符槽位
                update_bindless_texture(tex, i);
            }

            _renderables = scene.subMeshes;
            _instanceCount = static_cast<uint32_t>(scene.subMeshes.size());

            std::vector<InstanceData> instances;
            instances.reserve(_instanceCount);

            for (uint32_t i = 0; i < _instanceCount; i++) {
                const SubMesh& sm = scene.subMeshes[i];
                InstanceData inst{};

                inst.modelMatrix = glm::mat4(1.0f);

                float matIDAsFloat;
                memcpy(&matIDAsFloat, &sm.materialIndex, sizeof(float));

                inst.aabbMin_MatID = glm::vec4(sm.aabbMin, matIDAsFloat);
                inst.aabbMax_Pad = glm::vec4(sm.aabbMax, 0.0f);
                inst.indexCount = sm.indexCount;
                inst.firstIndex = sm.firstIndex;
                inst.vertexOffset = sm.vertexOffset;

                instances.push_back(inst);
            }

            if (!_renderables.empty()) {
                size_t instanceBufferSize = instances.size() * sizeof(InstanceData);
                _instanceBuffer = assetManager.upload_buffer_async(
                    instanceBufferSize, instances.data(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

                std::vector<VkDrawIndexedIndirectCommand> indirectCommands;
                indirectCommands.reserve(_instanceCount);

                for (uint32_t i = 0; i < _instanceCount; i++) {
                    const SubMesh& sm = scene.subMeshes[i];
                    VkDrawIndexedIndirectCommand cmd{};
                    cmd.indexCount = sm.indexCount;     // 这个网格有多少个顶点索引
                    cmd.instanceCount = 1;                 // 默认画1个（ComputeShader 剔除时会改成0）
                    cmd.firstIndex = sm.firstIndex;     // 索引偏移
                    cmd.vertexOffset = sm.vertexOffset;   // 顶点偏移
                    cmd.firstInstance = i;                 // Shader 中的 gl_InstanceIndex (对应 InstanceData 数组)

                    indirectCommands.push_back(cmd);
                }

                size_t indirectBufferSize = _instanceCount * sizeof(VkDrawIndexedIndirectCommand);

                // 注意用法：作为 SSBO 供 Compute 写入，同时作为 Indirect 缓冲供 Draw 读取
                _drawIndirectBuffer = assetManager.upload_buffer_async(
                    indirectBufferSize, indirectCommands.data(),
                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            }

            // --- 5. 更新全局 Bindless 描述符集 ---
            // 注意：这里 _sceneTextures 等现在已经是包含真实句柄的 AllocatedImage 了
            // 它们的数据正在后台被搬运，但不妨碍我们在 CPU 侧先把 Descriptor Set 绑好
            update_global_descriptor_set();

            _deletionQueue.push_function([=, this]() {
                VmaAllocator allocator = _renderDevice->get_allocator();
                VkDevice device = _renderDevice->get_device();

                // 销毁网格 Buffer
                vmaDestroyBuffer(allocator, _mainMeshBuffers.vertexBuffer.buffer, _mainMeshBuffers.vertexBuffer.allocation);
                vmaDestroyBuffer(allocator, _mainMeshBuffers.indexBuffer.buffer, _mainMeshBuffers.indexBuffer.allocation);

                // 销毁 SSBO 与 Indirect Buffer
                if (_materialBuffer.buffer != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(allocator, _materialBuffer.buffer, _materialBuffer.allocation);
                }
                if (_instanceBuffer.buffer != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(allocator, _instanceBuffer.buffer, _instanceBuffer.allocation);
                }
                if (_drawIndirectBuffer.buffer != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(allocator, _drawIndirectBuffer.buffer, _drawIndirectBuffer.allocation);
                }

                // 销毁所有纹理
                for (auto& tex : _sceneTextures) {
                    vkDestroyImageView(device, tex.view, nullptr);
                    vmaDestroyImage(allocator, tex.image, tex.allocation);
                }
                });

            std::cout << "[SceneRenderer] Scene data queued for transfer successfully." << std::endl;
        }

    } // namespace Renderer
} // namespace Aero