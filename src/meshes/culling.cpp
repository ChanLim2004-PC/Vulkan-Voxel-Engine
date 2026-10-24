#include "../../header/culling.h"
#include "../../header/voxeldata.h"
#include "../../header/vk_engine.h"

void ExecuteCulling::dispatch_to_buffer( VkCommandBuffer cmd, CullUniformBufferData* cullUniformBufferData, ChunkSystem* chunkSystem, CullComputePipelineData* cullCompute, VkPipelineLayout* layout, GPUMeshBuffers* meshBuffer){
    VkMemoryBarrier preFillBarrier{};
    preFillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    preFillBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    preFillBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &preFillBarrier, 0, nullptr, 0, nullptr);

    vkCmdFillBuffer(cmd, meshBuffer->drawCounterBuffer.buffer, 0, sizeof(uint32_t), 0);

    VkMemoryBarrier postFillBarrier{};
    postFillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    postFillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    postFillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &postFillBarrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullCompute->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullCompute->layout, 0, 1, &cullUniformBufferData->cullShaderDescriptor, 0, nullptr);

    CullComputePushConstant push_constants{};
    push_constants.finalIndirectBuffer = meshBuffer->finalIndirectBufferAddress;
    push_constants.ChunkMetaDataBuffer = meshBuffer->chunkMetaBufferAddress;
    push_constants.drawCounterBuffer = meshBuffer->drawCounterBufferAddress;
    push_constants.total_chunk_count = chunkSystem->indirect_buffer_size;

    vkCmdPushConstants(cmd, *layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullComputePushConstant), &push_constants);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullCompute->layout, 0, 1, &cullUniformBufferData->cullShaderDescriptor, 0, nullptr);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1, &barrier,0, nullptr,0, nullptr);

    //여기서 소수가 나오면 올려치기를 함
    uint32_t groupCountX = (chunkSystem->indirect_buffer_size + 63) / 64;
    vkCmdDispatch(cmd, std::max(1u, groupCountX), 1, 1);
    barrier_helper(cmd);
}

void ExecuteCulling::barrier_helper(VkCommandBuffer cmd){
    // Compute -> Vertex 바리어
    VkMemoryBarrier2 memoryBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memoryBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    memoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &memoryBarrier;
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

void ExecuteCulling::execute_culling(VulkanEngine* engine, CullUniformBufferData* cullUniformBufferData, ChunkSystem* chunkSystem, CullComputePipelineData* cullCompute, VkCommandBuffer cmd, VkPipelineLayout* layout, GPUMeshBuffers* meshBuffer){
    dispatch_to_buffer(cmd, cullUniformBufferData, chunkSystem, cullCompute, layout, meshBuffer);
}
