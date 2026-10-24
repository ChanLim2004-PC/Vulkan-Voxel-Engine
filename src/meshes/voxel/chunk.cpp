#include "../../../header/voxeldata.h"
#include "../../../header/vk_engine.h"

#include "fmt/base.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int4.hpp"
#include "glm/ext/vector_uint3.hpp"
#include <cstdint>
#include <vector>
#include <fmt/core.h>
#include <fmt/core.h>
#include <fmt/color.h>
#include <vector>

//we dont use this fixedChunk
void ChunkSystem::initialize_buffer(VulkanEngine* engine, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer){
    void* mapped = nullptr;
    vmaMapMemory(engine->_allocator, meshBuffer->prevCordBuffer.allocation, &mapped);
    glm::vec4* coords = static_cast<glm::vec4*>(mapped);
    for (uint32_t i = 0; i < indirect_buffer_size; ++i) {
        coords[i] = glm::vec4(-99999999.f);
    }
    vmaFlushAllocation(engine->_allocator, meshBuffer->prevCordBuffer.allocation, 0, VK_WHOLE_SIZE);
    vmaUnmapMemory(engine->_allocator, meshBuffer->prevCordBuffer.allocation);
}
//시작하기 전 초기화
void ChunkSystem::initialize_buffer_fixedChunk(VulkanEngine* engine, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer){
    void* prevMapped = nullptr;
    vmaMapMemory(engine->_allocator, meshBuffer->prevCordBuffer.allocation, &prevMapped);
    glm::vec4* coords = static_cast<glm::vec4*>(prevMapped);
    for (uint32_t i = 0; i < indirect_buffer_size; ++i) {
        coords[i] = glm::vec4(-99999999.f);
	cpuPrevCoords.push_back(glm::ivec4(-99999999.f));
    }
    vmaFlushAllocation(engine->_allocator, meshBuffer->prevCordBuffer.allocation, 0, VK_WHOLE_SIZE);
    vmaUnmapMemory(engine->_allocator, meshBuffer->prevCordBuffer.allocation);

    ChunkMetaData* chunkMetaMapped = nullptr;
    vmaMapMemory(engine->_allocator, meshBuffer->chunkMetaBuffer.allocation, (void**)&chunkMetaMapped);
    for (uint32_t i = 0; i < indirect_buffer_size; ++i) {
	chunkMetaMapped[i].drawCmd.firstVertex = i * max_buffer;
	chunkMetaMapped[i].drawCmd.vertexCount   = 0u;
	chunkMetaMapped[i].drawCmd.instanceCount = 0u; 
	chunkMetaMapped[i].drawCmd.firstInstance = 0u;

	chunkMetaMapped[i].chunkID = i;
	chunkMetaMapped[i].flags = 0u;
    }
    vmaFlushAllocation(engine->_allocator, meshBuffer->chunkMetaBuffer.allocation, 0, VK_WHOLE_SIZE);
    vmaUnmapMemory(engine->_allocator, meshBuffer->chunkMetaBuffer.allocation);
}
glm::vec4 ChunkSystem::compute_trigger(VulkanEngine* engine, glm::vec4 origin, float chunk_size, float chunk_num, glm::vec3 current_player_position) {
    float half_chunks = std::floor(chunk_num * 0.5f);
    
    // 현재 플레이어가 서 있는 청크 좌표
    int player_chunk_x = static_cast<int>(std::floor(current_player_position.x / chunk_size));
    int player_chunk_z = static_cast<int>(std::floor(current_player_position.z / chunk_size));

    // 현재 그리드의 시작 청크 좌표
    int origin_chunk_x = static_cast<int>(std::floor(origin.x / chunk_size));
    int origin_chunk_z = static_cast<int>(std::floor(origin.z / chunk_size));

    // 그리드의 중심 청크
    int center_chunk_x = origin_chunk_x + static_cast<int>(half_chunks);
    int center_chunk_z = origin_chunk_z + static_cast<int>(half_chunks);

    int diff_x = player_chunk_x - center_chunk_x;
    int diff_z = player_chunk_z - center_chunk_z;

    // 플레이어가 중심 청크에서 1청크 이상 벗어나면 그리드를 이동
    if (diff_x != 0 || diff_z != 0) {
        origin.x += static_cast<float>(diff_x) * chunk_size;
        origin.z += static_cast<float>(diff_z) * chunk_size;
        engine->_chunkIntialized = true;
    }
    return origin;
}

glm::ivec3 safeMod3D(glm::ivec3 coord, glm::ivec3 dim) {
    glm::vec3 c = glm::vec3(coord);
    glm::vec3 d = glm::vec3(dim);
    return glm::ivec3(c - (d * glm::floor(c / d)));
}
void ChunkSystem::update_dirty_chunks(VulkanEngine* engine) {
    dirtyChunkIndices.clear();
    dirty_chunk_count = 0;
    // push.inputStartPosition 과 동일한 기준점
    glm::ivec3 sp = glm::ivec3(floor(engine->starting_position / 32.0f));

    for (uint32_t z = 0; z < gridDim.z; ++z) {
	for (uint32_t y = 0; y < gridDim.y; ++y) {
	    for (uint32_t x = 0; x < gridDim.x; ++x) {
		glm::ivec3 worldChunkCoord = sp + glm::ivec3(x, y, z);
		glm::ivec3 slot3D = safeMod3D(worldChunkCoord, glm::ivec3(gridDim));

		uint32_t chunkID = slot3D.x + slot3D.y * gridDim.x + slot3D.z * (gridDim.x * gridDim.y);
		glm::ivec4 currentCord = glm::ivec4(worldChunkCoord, 1);
		if (cpuPrevCoords[chunkID] != currentCord) {
		    dirtyChunkIndices.push_back(chunkID);
		    dirty_chunk_count++;
		    cpuPrevCoords[chunkID] = currentCord; // 다음 프레임을 위해 갱신
		}
	    }
	}
    }
}
//GPU Preifx
/*void ChunkSystem::dispatchToBuffer(VulkanEngine* engine, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer){
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_voxelCountPipeline.pipeline);
    //MarchingCube Tables UBO
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_voxelCountPipeline.layout, 0, 1, &engine->_marchingCubeDescriptor, 0, nullptr);
    VoxelCountPushConstants countPush{};
    countPush.chunkMetaBuffer = meshBuffer->chunkMetaBufferAddress;
    countPush.prevCordBuffer = meshBuffer->prevCordBufferAddress;
    countPush.inputStartPosition = engine->starting_position;
    countPush.chunkGridDim = glm::uvec4(gridDim, 0);

    // Transfer -> Compute 바리어 vkCmdFillBuffer등이 update될때까지 기다린다
    VkMemoryBarrier initBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    initBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    initBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &initBarrier, 0, nullptr, 0, nullptr);
    vkCmdPushConstants(cmd, engine->_voxelCountPipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelCountPushConstants), &countPush);
    vkCmdDispatch(cmd, gridDim.x, gridDim.y, gridDim.z);
    barrier_helper(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_prefixPipeline.pipeline);
    PrefixPushConstants prefixPush{};
    prefixPush.chunkMetaBuffer = meshBuffer->chunkMetaBufferAddress;
    prefixPush.totalChunkCount = indirect_buffer_size;
    vkCmdPushConstants(cmd, engine->_prefixPipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefixPushConstants), &prefixPush);
    vkCmdDispatch(cmd, (prefixPush.totalChunkCount + 1023) / 1024, 1, 1);
    barrier_helper(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->voxelComputeEffect.pipeline);
    //MarchingCube Tables UBO
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_voxeldataPipelineLayout, 0, 1, &engine->_marchingCubeDescriptor, 0, nullptr);
    GPUDrawPushConstants push_constants{};
    push_constants.outputVertexBuffer = meshBuffer->vertexBufferAddress;
    push_constants.chunlMetaData = meshBuffer->chunkMetaBufferAddress;
    push_constants.starting_push_constant = engine->starting_position;
    push_constants.girdDim = glm::uvec4(gridDim, 1.0f);
    vkCmdPushConstants(cmd, engine->_voxeldataPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
    vkCmdDispatch(cmd, gridDim.x, gridDim.y, gridDim.z);
    barrier_helper(cmd);
}*/
//CPU prefix
/*void ChunkSystem::dispatchToBuffer(VulkanEngine* engine, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_voxelCountPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_voxelCountPipeline.layout, 0, 1, &engine->_marchingCubeDescriptor, 0, nullptr);
    
    VoxelCountPushConstants countPush{};
    countPush.chunkMetaBuffer = meshBuffer->chunkMetaBufferAddress;
    countPush.prevCordBuffer = meshBuffer->prevCordBufferAddress;
    countPush.inputStartPosition = engine->starting_position;
    countPush.chunkGridDim = glm::uvec4(gridDim, 0);

    VkMemoryBarrier initBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    initBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    initBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &initBarrier, 0, nullptr, 0, nullptr);

    vkCmdPushConstants(cmd, engine->_voxelCountPipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelCountPushConstants), &countPush);
    vkCmdDispatch(cmd, gridDim.x, gridDim.y, gridDim.z);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    // 엔진의 그래픽스/컴퓨트 큐에 제출
    vkQueueSubmit(engine->_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

    // GPU가 Pass 1 연산을 끝내서 vertexCount를 버퍼에 다 쓸 때까지 CPU 대기
    vkDeviceWaitIdle(engine->_device);

    ChunkMetaData* meta = nullptr;
    vmaMapMemory(engine->_allocator, meshBuffer->chunkMetaBuffer.allocation, (void**)&meta);
    vmaInvalidateAllocation(engine->_allocator, meshBuffer->chunkMetaBuffer.allocation, 0, VK_WHOLE_SIZE);

    uint32_t totalChunks = gridDim.x * gridDim.y * gridDim.z;
    uint32_t runningOffset = 0;

    for (uint32_t i = 0; i < totalChunks; i++) {
        uint32_t count = meta[i].drawCmd.vertexCount;
        meta[i].drawCmd.firstVertex = runningOffset;
        meta[i].drawCmd.instanceCount = (count > 0u) ? 1u : 0u;
        meta[i].drawCmd.firstInstance = 0u;
        runningOffset += count;
    }

    vmaFlushAllocation(engine->_allocator, meshBuffer->chunkMetaBuffer.allocation, 0, VK_WHOLE_SIZE);
    vmaUnmapMemory(engine->_allocator, meshBuffer->chunkMetaBuffer.allocation);

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    // 단발성이거나 이전 기록을 리셋하고 다시 쓸 때
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; 
    vkBeginCommandBuffer(cmd, &beginInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->voxelComputeEffect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_voxeldataPipelineLayout, 0, 1, &engine->_marchingCubeDescriptor, 0, nullptr);
    
    GPUDrawPushConstants push_constants{};
    push_constants.outputVertexBuffer = meshBuffer->vertexBufferAddress;
    push_constants.chunlMetaData = meshBuffer->chunkMetaBufferAddress;
    push_constants.starting_push_constant = engine->starting_position;
    push_constants.girdDim = glm::uvec4(gridDim, 1.0f);
    vkCmdPushConstants(cmd, engine->_voxeldataPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
    
    vkCmdDispatch(cmd, gridDim.x, gridDim.y, gridDim.z);
    barrier_helper(cmd);
}*/

void ChunkSystem::dispatchToBuffer(VulkanEngine* engine, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer) {
    if (engine->_initialize_once) { VkMemoryBarrier initBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER }; initBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    initBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
    initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, 
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
        0, 1, &initBarrier, 0, nullptr, 0, nullptr);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_voxelCountPipeline.pipeline);
    //MarchingCube Tables UBO
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_voxelCountPipeline.layout, 0, 1, &engine->_marchingCubeDescriptor, 0, nullptr);
    VoxelCountPushConstants countPush{};
    countPush.chunkMetaBuffer = meshBuffer->chunkMetaBufferAddress;
    countPush.prevCordBuffer = meshBuffer->prevCordBufferAddress;
    countPush.inputStartPosition = engine->starting_position;
    countPush.chunkGridDim = glm::uvec4(gridDim, 1.0f);
    vkCmdPushConstants(cmd, engine->_voxelCountPipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelCountPushConstants), &countPush);
    vkCmdDispatch(cmd, gridDim.x, gridDim.y, gridDim.z);
    barrier_helper(cmd);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->voxelComputeEffect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->_voxeldataPipelineLayout, 0, 1, &engine->_marchingCubeDescriptor, 0, nullptr);
    
    GPUDrawPushConstants push_constants{};
    push_constants.outputVertexBuffer = meshBuffer->vertexBufferAddress;
    push_constants.chunlMetaData = meshBuffer->chunkMetaBufferAddress;
    push_constants.rangeBuffer = meshBuffer->indirectRangeBufferAddress;
    push_constants.starting_push_constant = engine->starting_position;
    push_constants.girdDim = glm::uvec4(gridDim, 1.0f);
    vkCmdPushConstants(cmd, engine->_voxeldataPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
    
    vkCmdDispatch(cmd, gridDim.x, gridDim.y, gridDim.z);
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
void ChunkSystem::frame_chunk_generation(VulkanEngine* engine, VoxelPipeline* voxeldata, VkCommandBuffer cmd, GPUMeshBuffers* meshBuffer) {
    if(engine->_initialize_once){
	engine->starting_position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	float total_grid_size = chunk_size * float(gridDim.x);
	float half_grid_chunks = std::floor(float(gridDim.x) * 0.5f); 
	float half_grid_size   = half_grid_chunks * chunk_size;
	engine->starting_position = glm::vec4(
	    std::floor(engine->mainCamera.position.x / chunk_size - half_grid_chunks) * chunk_size,
	    0.0f,
	    std::floor(engine->mainCamera.position.z / chunk_size - half_grid_chunks) * chunk_size,
	    1.0f
	);

	engine->compute_buffer();
	voxeldata->voxel_count_buffer_create(engine, meshBuffer, this);
	initialize_buffer_fixedChunk(engine, cmd, meshBuffer);
	engine->_chunkIntialized = true;
	engine->_initialize_once = false;
    }
    if (!engine->_chunkIntialized) {
        engine->starting_position = compute_trigger(engine, engine->starting_position, 32, chunkIndex, engine->mainCamera.position);
    }
    if(engine->_chunkIntialized){
	initCmds.resize(indirect_buffer_size);
	update_dirty_chunks(engine);
	dispatchToBuffer(engine, cmd, meshBuffer);
	return;
    }
}
