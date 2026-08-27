#include "../../../header/voxeldata.h"
#include "../../../header/vk_engine.h"

#include "fmt/base.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include <vector>
#include <fmt/core.h>

glm::vec4 ChunkSystem::compute_trigger(VulkanEngine* engine, glm::vec4 origin, float chunk_size, float chunk_num, glm::vec3 current_player_position){
    float half_size = (chunk_size * chunk_num)/2;
    glm::vec2 origin_xz = glm::vec2(origin.x, origin.z);
    glm::vec2 area_center = origin_xz + glm::vec2(half_size);

    glm::vec2 player_pos_xz = glm::vec2(current_player_position.x, current_player_position.z);
    glm::vec2 raw_offset = player_pos_xz - area_center; 

    float boundary_limit = (chunk_size * 0.5f);

    glm::ivec2 shift_direction = glm::ivec2(0, 0);

    if (std::abs(raw_offset.x) >= boundary_limit) {
        shift_direction.x = static_cast<int>(std::floor((raw_offset.x + boundary_limit) / chunk_size));
    }
    if (std::abs(raw_offset.y) >= boundary_limit) {
        shift_direction.y = static_cast<int>(std::floor((raw_offset.y + boundary_limit) / chunk_size));
    }

    if (shift_direction.x != 0 || shift_direction.y != 0) {
	glm::vec2 move_delta = glm::vec2(shift_direction) * chunk_size;
	origin.x += move_delta.x;
	origin.z += move_delta.y;
	engine->_chunkIntialized = true;
    }
    return origin;
}
void ChunkSystem::rebuild_offsets(glm::vec4 initial_offset) {
    cachedBatchOffsets.clear();
    float batch_size = chunk_size * batchIndex;
    //18/3 --> 6
    int loopCount = chunkIndex / batchIndex; 

    for (int y = 0; y < loopCount; y++) {
        for (int z = 0; z < loopCount; z++) {
            for (int x = 0; x < loopCount; x++) {
                glm::vec3 batch_vector_size = glm::vec3(batch_size * x, batch_size * y, batch_size * z);
                glm::vec3 temp_initial_offset = glm::vec3(initial_offset.x, initial_offset.y, initial_offset.z);
                glm::vec3 batch_offset = batch_vector_size + temp_initial_offset;
                cachedBatchOffsets.push_back(glm::vec4(batch_offset, 1.0f));
            }
        }
    }
    total_chunk_Index = cachedBatchOffsets.size();
}
void ChunkSystem::resetIndirectBuffer(VulkanEngine* engine, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer){
    initCmds[total_batch_Index].indexCount = 0;   
    initCmds[total_batch_Index].instanceCount = 1; 
    initCmds[total_batch_Index].firstIndex = 0;
    initCmds[total_batch_Index].vertexOffset = 0;
    initCmds[total_batch_Index].firstInstance = 0;
    VkDeviceSize dstOffset = sizeof(VkDrawIndexedIndirectCommand) * total_batch_Index;
    vkCmdUpdateBuffer(cmd, meshBuffer->indirectCmdBuffer.buffer, dstOffset, sizeof(VkDrawIndexedIndirectCommand), &initCmds[total_batch_Index]);

    VkMemoryBarrier fillBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fillBarrier, 0, nullptr, 0, nullptr);
}

void ChunkSystem::dispatchToBuffer(VulkanEngine* engine, VkCommandBuffer cmd, VkPipelineLayout layout, GPUMeshBuffers* meshBuffer, int total_batch_Index, glm::vec4 batch_position ){
    VkDrawIndexedIndirectCommand* indexcountData = nullptr;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->voxelComputeEffect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_voxeldataPipelineLayout, 0, 1, &engine->_marchingCubeDescriptor, 0, nullptr);

    GPUDrawPushConstants push_constants{};
    push_constants.outputVertexBuffer = meshBuffer->vertexBufferAddress;
    push_constants.outputIndexBuffer = meshBuffer->indexBufferAddress;
    push_constants.indirectCmdBuffer = meshBuffer->indirectCmdBufferAddress;
    push_constants.globalOffsetCmdBuffer = meshBuffer->globalOffsetBufferAddress;
    push_constants.indirectRangeBuffer = meshBuffer->indirectRangeBufferAddress;
    push_constants.indirectBufferNum = total_batch_Index;
    push_constants.starting_push_constant = batch_position;

    // Transfer -> Compute 바리어 vkCmdFillBuffer등이 update될때까지 기다린다
    VkMemoryBarrier initBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    initBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    initBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &initBarrier, 0, nullptr, 0, nullptr);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
    vkCmdDispatch(cmd, batchIndex, 6, batchIndex);

    barrier_helper(cmd);
}

void ChunkSystem::barrier_helper(VkCommandBuffer cmd){
    // Chunk Compute -> Cull Compute 바리어 
    VkMemoryBarrier2 memoryBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memoryBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    memoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &memoryBarrier;
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}
void ChunkSystem::frame_chunk_generation(VulkanEngine* engine, VkCommandBuffer cmd, VkPipelineLayout layout, GPUMeshBuffers* meshBuffer) {

    if(engine->start_scene){
	if (cachedBatchOffsets.empty()) {
	    initCmds.resize(indirect_buffer_size);
	    engine->compute_buffer();
            total_batch_Index = 0;
	    vkCmdFillBuffer(cmd, meshBuffer[_backBufferIdx].globalOffsetBuffer.buffer, 0, sizeof(uint32_t), 0);
	    rebuild_offsets(engine->starting_position);
	}
	//매 프레임마다 reset을 시켜주는 거임
	resetIndirectBuffer(engine, cmd, &meshBuffer[_frontBufferIdx]);
        batch_position = cachedBatchOffsets[total_batch_Index];

	dispatchToBuffer(engine, cmd, layout, &meshBuffer[_frontBufferIdx], total_batch_Index, batch_position);

        total_batch_Index++;
        if (total_batch_Index >= cachedBatchOffsets.size()) {
	    engine->start_scene = false;
	    engine->_chunkIntialized = false;
	    engine->_chunkFinishedInitilized = true;
	    total_batch_Index = 0;
	    cachedBatchOffsets.clear();
        }
	return;
    }

    if (!engine->_chunkIntialized) {
        engine->starting_position = compute_trigger(engine, engine->starting_position, 32, chunkIndex, engine->mainCamera.position);
    }

    if (engine->_chunkIntialized) {
	if (cachedBatchOffsets.empty()) {
	    initCmds.resize(indirect_buffer_size);
	    engine->compute_buffer();
	    vkCmdFillBuffer(cmd, meshBuffer[_backBufferIdx].globalOffsetBuffer.buffer, 0, sizeof(uint32_t), 0);
	    rebuild_offsets(engine->starting_position);
	}
	resetIndirectBuffer(engine, cmd, &meshBuffer[_backBufferIdx]);
        batch_position = cachedBatchOffsets[total_batch_Index];

	//여기서 일단은 backFaceBuffer 뒤에서 일하는 친구들을 먼저 실행을 시킴
	dispatchToBuffer(engine, cmd, layout, &meshBuffer[_backBufferIdx], total_batch_Index, batch_position);
        total_batch_Index++;
        if (total_batch_Index >= cachedBatchOffsets.size()) {
	    std::swap(_frontBufferIdx, _backBufferIdx);
            total_batch_Index = 0;
            engine->_chunkIntialized = false; 
	    engine->_chunkFinishedInitilized = true;
	    cachedBatchOffsets.clear();
        }
	return;
    }
}
