#include "SceneRenderer.h"
#include "../vk_initializers.h"
#include "../vk_pipelines.h"
#include <stb_image.h>
#include <iostream>
#include <array>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace Aero {
    namespace Renderer {

        struct ComputePushConstants {
            glm::vec4 planes[6];
            uint32_t instanceCount;
        };

        std::array<glm::vec4, 6> get_frustum_planes(const glm::mat4& viewProj) {
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

        GPUMeshBuffers Aero::Renderer::SceneRenderer::upload_mesh_data(const SceneData& scene) {
            VmaAllocator allocator = _renderDevice->get_allocator();

            GPUMeshBuffers outBuffers;

            const size_t vertexBufferSize = scene.vertices.size() * sizeof(Vertex);
            const size_t indexBufferSize = scene.indices.size() * sizeof(uint32_t);
            const size_t totalBufferSize = vertexBufferSize + indexBufferSize;

            VkBufferCreateInfo stagingBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            stagingBufferInfo.size = totalBufferSize;
            stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            VmaAllocationCreateInfo stagingAllocInfo = {};
            stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            AllocatedBuffer stagingBuffer;
            VmaAllocationInfo stagingAllocResult;
            VK_CHECK(vmaCreateBuffer(allocator, &stagingBufferInfo, &stagingAllocInfo,
                &stagingBuffer.buffer, &stagingBuffer.allocation, &stagingAllocResult));

            void* mappedData = stagingAllocResult.pMappedData;
            memcpy(mappedData, scene.vertices.data(), vertexBufferSize);
            memcpy(static_cast<char*>(mappedData) + vertexBufferSize, scene.indices.data(), indexBufferSize);

            VkBufferCreateInfo vboInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            vboInfo.size = vertexBufferSize;
            vboInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VkBufferCreateInfo iboInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            iboInfo.size = indexBufferSize;
            iboInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VmaAllocationCreateInfo vmaAllocInfo = {};
            vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

            VK_CHECK(vmaCreateBuffer(allocator, &vboInfo, &vmaAllocInfo,
                &outBuffers.vertexBuffer.buffer, &outBuffers.vertexBuffer.allocation, nullptr));

            VK_CHECK(vmaCreateBuffer(allocator, &iboInfo, &vmaAllocInfo,
                &outBuffers.indexBuffer.buffer, &outBuffers.indexBuffer.allocation, nullptr));

            _renderDevice->immediate_submit([&](VkCommandBuffer cmd) {
                VkBufferCopy vertexCopy = { 0, 0, vertexBufferSize };
                vkCmdCopyBuffer(cmd, stagingBuffer.buffer, outBuffers.vertexBuffer.buffer, 1, &vertexCopy);

                VkBufferCopy indexCopy = { vertexBufferSize, 0, indexBufferSize };
                vkCmdCopyBuffer(cmd, stagingBuffer.buffer, outBuffers.indexBuffer.buffer, 1, &indexCopy);
                });

            vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);

            return outBuffers;
        }

        AllocatedBuffer Aero::Renderer::SceneRenderer::upload_ssbo_data(size_t bufferSize, const void* data) {
            VmaAllocator allocator = _renderDevice->get_allocator();

            // 注意这里第一行不用再算 bufferSize 了，直接用传进来的
            VkBufferCreateInfo ssboInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            ssboInfo.size = bufferSize;
            ssboInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VmaAllocationCreateInfo vmaAllocInfo = {};
            vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

            AllocatedBuffer ssboBuffer;
            VK_CHECK(vmaCreateBuffer(allocator, &ssboInfo, &vmaAllocInfo,
                &ssboBuffer.buffer, &ssboBuffer.allocation, nullptr));

            VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            stagingInfo.size = bufferSize;
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            VmaAllocationCreateInfo stagingAllocInfo = {};
            stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            AllocatedBuffer stagingBuffer;
            VmaAllocationInfo stagingAllocResult;
            VK_CHECK(vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                &stagingBuffer.buffer, &stagingBuffer.allocation, &stagingAllocResult));

            // 使用传入的 data 指针进行拷贝
            memcpy(stagingAllocResult.pMappedData, data, bufferSize);

            _renderDevice->immediate_submit([&](VkCommandBuffer cmd) {
                VkBufferCopy copyRegion = { 0, 0, bufferSize };
                vkCmdCopyBuffer(cmd, stagingBuffer.buffer, ssboBuffer.buffer, 1, &copyRegion);
                });

            vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);

            return ssboBuffer;
        }

        AllocatedImage Aero::Renderer::SceneRenderer::upload_texture(void* pixels, int width, int height, VkFormat format) {
            VmaAllocator allocator = _renderDevice->get_allocator();

            uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
            VkExtent3D imageExtent = { (uint32_t)width, (uint32_t)height, 1 };
            VkDeviceSize imageSize = width * height * 4;

            VkImageCreateInfo dimg_info{};
            dimg_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            dimg_info.imageType = VK_IMAGE_TYPE_2D;
            dimg_info.format = format;
            dimg_info.extent = imageExtent;
            dimg_info.mipLevels = mipLevels;
            dimg_info.arrayLayers = 1;
            dimg_info.samples = VK_SAMPLE_COUNT_1_BIT;
            dimg_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            dimg_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

            VmaAllocationCreateInfo dimg_allocinfo{};
            dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

            AllocatedImage newImage;
            newImage.imageFormat = format;
            newImage.imageExtent = imageExtent;
            VK_CHECK(vmaCreateImage(allocator, &dimg_info, &dimg_allocinfo,
                &newImage.image, &newImage.allocation, nullptr));

            VkBufferCreateInfo stagingInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            stagingInfo.size = imageSize;
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            VmaAllocationCreateInfo stagingAllocInfo = {};
            stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            AllocatedBuffer stagingBuffer;
            VmaAllocationInfo stagingAllocResult;
            VK_CHECK(vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, &stagingAllocResult));

            memcpy(stagingAllocResult.pMappedData, pixels, static_cast<size_t>(imageSize));

            // 3. 异步提交：Transition Layout (Undefined -> TransferDst) -> Copy Buffer To Image -> Transition Layout (TransferDst -> ShaderReadOnly)
            _renderDevice->immediate_submit([&](VkCommandBuffer cmd) {
                vkinit::transition_image_mip(cmd, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, mipLevels);

                VkBufferImageCopy copyRegion = {};
                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.imageSubresource.mipLevel = 0;
                copyRegion.imageSubresource.baseArrayLayer = 0;
                copyRegion.imageSubresource.layerCount = 1;
                copyRegion.imageExtent = imageExtent;
                vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

                int32_t mipWidth = width;
                int32_t mipHeight = height;

                for (uint32_t i = 1; i < mipLevels; i++) {
                    vkinit::transition_image_mip(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, i - 1, 1);

                    VkImageBlit blit{};
                    blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
                    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    blit.srcSubresource.mipLevel = i - 1;
                    blit.srcSubresource.layerCount = 1;

                    blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
                    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    blit.dstSubresource.mipLevel = i;
                    blit.dstSubresource.layerCount = 1;

                    vkCmdBlitImage(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                    vkinit::transition_image_mip(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, i - 1, 1);

                    if (mipWidth > 1) mipWidth /= 2;
                    if (mipHeight > 1) mipHeight /= 2;
                }

                vkinit::transition_image_mip(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels - 1, 1);
                });

            vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = newImage.image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = mipLevels;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            VK_CHECK(vkCreateImageView(_renderDevice->get_device(), &view_info, nullptr, &newImage.view));

            return newImage;
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

        void Aero::Renderer::SceneRenderer::upload_scene(const SceneData& scene) {
            std::cout << "[AeroEngine] Starting scene upload to GPU..." << std::endl;

            VmaAllocator allocator = _renderDevice->get_allocator();

            uint32_t whitePixel = 0xFFFFFFFF; // RGBA 全部为 255
            AllocatedImage defaultTexture = upload_texture(&whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM);
            _sceneTextures.push_back(defaultTexture); // 交给现有的资源数组统一管理销毁

            // 将 1024 个槽位全部初始化为安全的安全贴图
            for (uint32_t i = 0; i < 4096; ++i) {
                update_bindless_texture(defaultTexture, i);
            }

            _mainMeshBuffers = upload_mesh_data(scene);
            _renderables = scene.subMeshes;

            _materialBuffer = upload_ssbo_data(scene.materials.size() * sizeof(MaterialParams), scene.materials.data());

            // 更新全局 Descriptor Set 的 Binding 0 (材质 SSBO)
            _instanceCount = static_cast<uint32_t>(scene.subMeshes.size());
            std::vector<InstanceData> instances(_instanceCount);
            for (size_t i = 0; i < _instanceCount; ++i) {
                const SubMesh& mesh = scene.subMeshes[i];
                instances[i].modelMatrix = glm::mat4(1.0f); // 当前 glTF 解析器还未提取节点 Transform，先填单位阵

                //利用 float 存储 int，在 Shader 里用 floatBitsToUint 转回来，保证 vec4 对齐
                float matIDAsFloat;
                uint32_t matID = mesh.materialIndex;
                memcpy(&matIDAsFloat, &matID, sizeof(float));

                instances[i].aabbMin_MatID = glm::vec4(mesh.aabbMin, matIDAsFloat);
                instances[i].aabbMax_Pad = glm::vec4(mesh.aabbMax, 0.0f);
                instances[i].indexCount = mesh.indexCount;
                instances[i].firstIndex = mesh.firstIndex;
                instances[i].vertexOffset = mesh.vertexOffset;
            }

            _instanceBuffer = upload_ssbo_data(instances.size() * sizeof(InstanceData), instances.data());

            VkBufferCreateInfo indirectInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            indirectInfo.size = _instanceCount * sizeof(VkDrawIndexedIndirectCommand);
            indirectInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VmaAllocationCreateInfo indirectAllocInfo = {};
            indirectAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

            VK_CHECK(vmaCreateBuffer(allocator, &indirectInfo, &indirectAllocInfo,
                &_drawIndirectBuffer.buffer, &_drawIndirectBuffer.allocation, nullptr));

            // 更新全局 Descriptor Set 的 Binding 0 (材质) 和 Binding 2 (Instance)
            update_global_descriptor_set();

            //遍历图片并上传到 GPU 的 Bindless 数组
            int loadedTextureCount = 0;
            for (size_t i = 0; i < scene.images.size(); i++) {
                const LoadedImage& img = scene.images[i];

                if (img.pixels != nullptr) {
                    AllocatedImage gpuImage = upload_texture(img.pixels, img.width, img.height, VK_FORMAT_R8G8B8A8_UNORM);

                    update_bindless_texture(gpuImage, static_cast<uint32_t>(i));

                    _sceneTextures.push_back(gpuImage);

                    stbi_image_free(img.pixels);
                    loadedTextureCount++;
                }
            }

            _deletionQueue.push_function([=]() {
                vmaDestroyBuffer(allocator, _drawIndirectBuffer.buffer, _drawIndirectBuffer.allocation);
                vmaDestroyBuffer(allocator, _mainMeshBuffers.vertexBuffer.buffer, _mainMeshBuffers.vertexBuffer.allocation);
                vmaDestroyBuffer(allocator, _mainMeshBuffers.indexBuffer.buffer, _mainMeshBuffers.indexBuffer.allocation);
                vmaDestroyBuffer(allocator, _materialBuffer.buffer, _materialBuffer.allocation);
                vmaDestroyBuffer(allocator, _instanceBuffer.buffer, _instanceBuffer.allocation);
                for (const AllocatedImage& img : _sceneTextures) {
                    vkDestroyImageView(_renderDevice->get_device(), img.view, nullptr);
                    vmaDestroyImage(allocator, img.image, img.allocation);
                }
                });

            std::cout << "[AeroEngine] Successfully uploaded scene to GPU! (Textures loaded: " << loadedTextureCount << ")" << std::endl;
        }

    } // namespace Renderer
} // namespace Aero