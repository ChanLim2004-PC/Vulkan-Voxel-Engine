#pragma once
#include "vk_types.h"

class VulkanEngine;
class ChunkSystem;

class RayTracingTool{
private:
    void createBLASStructure(VulkanEngine* engine, VkCommandBuffer* cmd, GPUMeshBuffers* meshBuffer, ChunkSystem* system);
    void accelerationBuffer(VulkanEngine* engine, VkCommandBuffer* cmd, RayTracingBuffer* raytracingBuffer);
    void createAccelerationBuffer(VulkanEngine* engine, ChunkSystem* system, RayTracingBuffer* raytracingBuffer, VkAccelerationStructureBuildSizesInfoKHR* tlasBuildSizes);
    void createTLAStructure(VulkanEngine* engine, VkCommandBuffer* cmd, ChunkSystem* system, GPUMeshBuffers* meshBuffer, RayTracingBuffer* raytracingBuffer);
public:
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR_ = nullptr;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR_ = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR_ = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR_ = nullptr; 
    PFN_vkGetAccelerationStructureDeviceAddressKHR  vkGetAccelerationStructureDeviceAddressKHR_= nullptr;
    PFN_vkCmdBuildAccelerationStructuresIndirectKHR  vkCmdBuildAccelerationStructuresIndirectKHR_= nullptr;

    void initRayTracingFunctions(VkDevice _device);
    void activateRayTacing(VulkanEngine* engine, ChunkSystem* system, VkCommandBuffer* cmd, GPUMeshBuffers* meshBuffer, RayTracingBuffer* raytracingBuffer);
    void setupRaytracing();
    void bindDescriptorSet(VulkanEngine* engine, VkCommandBuffer* cmd, RayTracingBuffer* raytracingBuffer);
};
