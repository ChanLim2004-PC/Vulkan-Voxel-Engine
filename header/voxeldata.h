#pragma once
#include "culling.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "vk_types.h"
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <vector>
#include <vulkan/vulkan.h>

class VulkanEngine;

struct VoxelModel{
public:
    glm::mat4 singleTileModel;
};
struct VoxelDispatchPushConstants {
    //전체적인 시작 offset
    uint32_t chunkOffset;
    //매프레임마다 생성할 chunk총 갯수
    uint32_t batchSize;
};
class ChunkSystem{
private:
    int chunkIndex = 8;
    int batchIndex = 2;
    float chunk_size = 32.0f;
    int total_chunk_Index = 0;
    int current_bathch_Index = 0;
    uint64_t total_indicies_count = 0;
    uint32_t current_indicies = 0;
    uint32_t current_first_idx = 0;

public:
    std::vector<glm::vec4> cachedBatchOffsets;
    glm::vec4 batch_position;
    uint32_t indirect_buffer_size = (chunkIndex*chunkIndex*chunkIndex)/(batchIndex*batchIndex*batchIndex); 
    int total_batch_Index = 0;
    GPUMeshBuffers _meshBuffers[2]; 
    //앞에 작업
    int _frontBufferIdx = 0;
    //뒤에 작업
    int _backBufferIdx  = 1;
    std::vector<VkDrawIndexedIndirectCommand> initCmds;
    //indirect Buffer갯수
    glm::vec4 compute_trigger(VulkanEngine* engine, glm::vec4 origin, float chunk_size, float chunk_num, glm::vec3 current_player_position);
    void rebuild_offsets(glm::vec4 inital_offset);
    void resetIndirectBuffer(VulkanEngine* engine, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer);
    void dispatchToBuffer(VulkanEngine* engine, VkCommandBuffer cmd, VkPipelineLayout layout, GPUMeshBuffers* meshBuffer, int total_batch_Index, glm::vec4 batch_position );
    void barrier_helper(VkCommandBuffer cmd);
    //things that goes into actual DrawCall
    void frame_chunk_generation(VulkanEngine* engine, VkCommandBuffer cmd, VkPipelineLayout layout, GPUMeshBuffers* meshBuffers);
};
