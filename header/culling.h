#pragma once
#include <vulkan/vulkan.h>
#include "cull_pipeline.h"
#include "vk_types.h"

//전방선언
class VulkanEngine;
class ChunkSystem;

class ExecuteCulling{
public:
    //매인 execution
    void execute_culling(VulkanEngine* engine, CullUniformBufferData* cullUniformBufferData, ChunkSystem* chunkSystem, CullComputePipelineData* cullCompute, VkCommandBuffer cmd, VkPipelineLayout* layout, GPUMeshBuffers* meshBuffer);
    //부수적인 함수들
    void dispatch_to_buffer(VkCommandBuffer cmd, CullUniformBufferData* cullUniformBufferData, ChunkSystem* chunkSystem, CullComputePipelineData* cullCompute, VkPipelineLayout* layout, GPUMeshBuffers* meshBuffer);
    void barrier_helper(VkCommandBuffer cmd);
};
