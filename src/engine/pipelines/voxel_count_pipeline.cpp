#include "../../../header/voxel_compute_pipeline.h"
#include "../../../header/vk_engine.h"
#include "../../../header/vk_pipeline.h"
#include "../../../header/vk_types.h"
#include "glm/ext/vector_float4.hpp"

void VoxelPipeline::voxel_count_buffer_create(VulkanEngine* engine, GPUMeshBuffers* gpuMeshBuffers, ChunkSystem* system){
    VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };

    //push_constant관련 데이터들을 여기서 생성을 한다. 
    //counter buffer creation
    gpuMeshBuffers->prevCordBuffer = engine->create_buffer(system->indirect_buffer_size * sizeof(glm::vec4), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    addressInfo.buffer = gpuMeshBuffers->prevCordBuffer.buffer;
    gpuMeshBuffers->prevCordBufferAddress = vkGetBufferDeviceAddress(engine->_device, &addressInfo);

    AllocatedBuffer temp_buffer1= gpuMeshBuffers->prevCordBuffer;
    engine->_mainDeletionQueue.push_function([=](){
	engine->destroy_buffer(temp_buffer1);
    });
}

void VoxelPipeline::noise_descriptor(VulkanEngine* engine, NoiseUniformBuffer* noiseUniformBuffer){
    noiseUniformBuffer->noiseUniformDescriptor = engine->globalDescriptorAllocator.allocate(engine->_device, noiseUniformBuffer->noiseUniformDescriptorLayout);
    noiseUniformBuffer->noiseUniformBuffer = engine->create_buffer(sizeof(GPUCullData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    DescriptorWriter writer;
    GPUCullData* cullUniformData = nullptr;
    vmaMapMemory(engine->_allocator, noiseUniformBuffer->noiseUniformBuffer.allocation, (void**)&cullUniformData);
    //여기에다가 너가 원하는 데이터를 알아서 넣으세요💥
    //*cullUniformData = here;
    vmaUnmapMemory(engine->_allocator, noiseUniformBuffer->noiseUniformBuffer.allocation);

    writer.write_buffer(0, noiseUniformBuffer->noiseUniformBuffer.buffer, sizeof(GPUCullData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(engine->_device, noiseUniformBuffer->noiseUniformDescriptor);

    AllocatedBuffer bufferToDestroy = noiseUniformBuffer->noiseUniformBuffer;
    engine->_mainDeletionQueue.push_function([=]() {
	engine->destroy_buffer(bufferToDestroy);
    });
}

void VoxelPipeline::init_voxel_count_pipeline(VulkanEngine* engine, VkPipelineLayout* pipelineLayout, VoxelComputePipeline* pipeline){
    VkPipelineLayoutCreateInfo voxelCountPipelineLayout{};
    voxelCountPipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    voxelCountPipelineLayout.pNext = nullptr;

    VkDescriptorSetLayout layout = engine->_marchingCubeDescriptorLayout;
    voxelCountPipelineLayout.pSetLayouts = &layout;
    voxelCountPipelineLayout.setLayoutCount = 1;

    VkPushConstantRange pushConstant{};
    //시작점
    pushConstant.offset = 0;
    //그냥 전체적인 사이즈 (ComputePushConstants구조체(engine.h))
    pushConstant.size = sizeof(VoxelCountPushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    //이제 단순히 디스크립터 뿐만 아닌 pushConstant도 추가한것이다
    voxelCountPipelineLayout.pPushConstantRanges = &pushConstant;
    voxelCountPipelineLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(engine->_device, &voxelCountPipelineLayout, nullptr, pipelineLayout));

    VkShaderModule voxel_count_comp;
    if(!vkutil::load_shader_module("./spv/voxel_count.comp.spv", engine->_device, &voxel_count_comp)){
	fmt::print("Error when building the voxel compute shader module");
    }

    //shader상세 정보들
    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    //실제 쉐이더 가져와서 넣는다
    stageinfo.module = voxel_count_comp;
    stageinfo.pName = "main";

    //이제 pipeline layout긜고 쉐이더 상새정보(stageinfo)연결
    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = *pipelineLayout;
    computePipelineCreateInfo.stage = stageinfo;

    //이제 compute shader를 실제로 가지고 온다 ㅇㅇ.
    pipeline->layout = *pipelineLayout;
    pipeline->name = "voxel_count_pipeline";
    VK_CHECK(vkCreateComputePipelines(engine->_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &pipeline->pipeline));

    vkDestroyShaderModule(engine->_device, voxel_count_comp, nullptr);
    engine->_mainDeletionQueue.push_function([=,this](){
	vkDestroyPipelineLayout(engine->_device, pipeline->layout, nullptr);
	vkDestroyPipeline(engine->_device, pipeline->pipeline, nullptr);
    });
};
