#pragma once
#include "vk_types.h"
#include <vulkan/vulkan.h>

class VulkanEngine;
class ChunkSystem;

struct VoxelComputePipeline {
    const char* name;
    VkPipeline pipeline;
    VkPipelineLayout layout;
};
struct NoiseUniformBuffer{
    VkDescriptorSetLayout noiseUniformDescriptorLayout;
    VkDescriptorSet noiseUniformDescriptor;
    AllocatedBuffer noiseUniformBuffer;
};

class VoxelPipeline{
public:
    //Pass 1
    void init_voxel_count_pipeline(VulkanEngine* engine, VkPipelineLayout* pipelineLayout, VoxelComputePipeline* pipeline);
    void voxel_count_buffer_create(VulkanEngine* engine, GPUMeshBuffers* gpuMeshBuffers, ChunkSystem* system);
    void noise_descriptor(VulkanEngine* engine, NoiseUniformBuffer* noiseUniformBuffer);

    //Pass 2
    void init_prefix_pipeline(VulkanEngine* engine, VkPipelineLayout* pipelineLayout, VoxelComputePipeline* pipeline);
};
