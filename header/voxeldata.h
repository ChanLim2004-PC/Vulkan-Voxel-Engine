#pragma once
#include "culling.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_uint3.hpp"
#include "vk_types.h"
#include "voxel_compute_pipeline.h"
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <vector>
#include <vulkan/vulkan.h>

class VulkanEngine;
class VoxelPipeline;

struct VoxelModel{
public:
    glm::mat4 singleTileModel;
};
class ChunkSystem{
private:
    int chunkIndex = 8;
    float chunk_size = 32.0f;
    int total_chunk_Index = 0;
    uint64_t total_indicies_count = 0;
    uint32_t current_indicies = 0;
    uint32_t current_first_idx = 0;

public:
    glm::uvec3 gridDim = glm::uvec3(chunkIndex, 4, chunkIndex);
    uint32_t indirect_buffer_size = gridDim.x * gridDim.y * gridDim.z;
    uint32_t dirty_chunk_count;
    uint32_t max_buffer = 25000;
    std::vector<uint32_t> dirtyChunkIndices;
    std::vector<glm::ivec4> cpuPrevCoords;

    GPUMeshBuffers _meshBuffers; 
    std::vector<VkDrawIndexedIndirectCommand> initCmds;
    glm::vec4 compute_trigger(VulkanEngine* engine, glm::vec4 origin, float chunk_size, float chunk_num, glm::vec3 current_player_position);
    void chunkManger(VulkanEngine* engine);
    void dispatchToBuffer(VulkanEngine* engine, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer);
    void barrier_helper(VkCommandBuffer cmd);
    //things that goes into actual DrawCall
    void frame_chunk_generation(VulkanEngine* engine, VoxelPipeline* voxeldata, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffers);

    void initialize_buffer(VulkanEngine* engine, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer);
    void initialize_buffer_fixedChunk(VulkanEngine* engine, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer);
    void verify_prefix_sum(VulkanEngine* engine, GPUMeshBuffers* meshBuffer);
    void update_dirty_chunks(VulkanEngine* engine);
};
