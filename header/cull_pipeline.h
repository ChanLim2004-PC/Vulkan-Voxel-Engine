#pragma once
#include <vulkan/vulkan.h>
#include "vk_scene.h"
#include "vk_types.h"

//전방 선언 여기서 가져다 쓸 확률이 나름 높을 수 있음
class VulkanEngine;
class ChunkSystem;

struct CullUniformBufferData{
    VkDescriptorSetLayout cullShaderDescriptorLayout;
    VkDescriptorSet cullShaderDescriptor;
    AllocatedBuffer cullShaderBuffer;
};

struct CullComputePipelineData {
    const char* name;
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

struct CullComputePipeline{
    void culling_create_buffer(VulkanEngine* engine, ChunkSystem* chunkSystem);
    GPUCullData cull_unifrom_data(GPUSceneData sceneData, ChunkSystem* chunkSystem, VkExtent2D* _windowExtent);
    void init_cullcompute_pipeline(VulkanEngine* engine, VkPipelineLayout* _cullPipelineLayout, CullComputePipelineData* cullCompute, CullUniformBufferData* cullUniformBuffer);
    void cull_compute_descriptor(VulkanEngine* engine, CullUniformBufferData* cullUniformBuffer, GPUSceneData sceneData, ChunkSystem* chunkSystem, VkExtent2D* _windowExtent);
};
