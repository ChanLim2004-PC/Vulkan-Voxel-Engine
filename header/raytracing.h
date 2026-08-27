#pragma once
#include "vk_types.h"

class VulkanEngine;
class ChunkSystem;

class RayTracingTool{
private:
    void createBLASStructure(VulkanEngine* engine, VkCommandBuffer* cmd, GPUMeshBuffers* meshBuffer, int current_vertcies_size);
    void accelerationBuffer(VulkanEngine* engine, VkCommandBuffer* cmd, RayTracingBuffer* raytracingBuffer);
    void createAccelerationBuffer(VulkanEngine* engine, RayTracingBuffer* raytracingBuffer, VkAccelerationStructureBuildSizesInfoKHR* tlasBuildSizes);
    void createTLAStructure(VulkanEngine* engine, VkCommandBuffer* cmd, GPUMeshBuffers* meshBuffer, RayTracingBuffer* raytracingBuffer);
public:
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR_ = nullptr;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR_ = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR_ = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR_ = nullptr; 
    PFN_vkGetAccelerationStructureDeviceAddressKHR  vkGetAccelerationStructureDeviceAddressKHR_= nullptr;
    PFN_vkCmdBuildAccelerationStructuresIndirectKHR  vkCmdBuildAccelerationStructuresIndirectKHR_= nullptr;

    void initRayTracingFunctions(VkDevice _device);
    void activateRayTacing(VulkanEngine* engine, VkCommandBuffer* cmd, GPUMeshBuffers* meshBuffer, RayTracingBuffer* raytracingBuffer, int BLAS_vertcies_size);
};
