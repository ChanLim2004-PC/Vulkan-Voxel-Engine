#include "../../../header/cull_pipeline.h"
#include "../../../header/vk_engine.h"
#include "../../../header/vk_pipeline.h"
#include <cstdint>

void CullComputePipeline::culling_create_buffer(VulkanEngine* engine, ChunkSystem* chunkSystem){
    //becuase it is double buffering
    for (int i = 0; i < 2; ++i) {
	//counter buffer creation
	chunkSystem->_meshBuffers[i].drawCounterBuffer = engine->create_buffer(sizeof(uint64_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
	VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	addressInfo.buffer = chunkSystem->_meshBuffers[i].drawCounterBuffer.buffer;
	chunkSystem->_meshBuffers[i].drawCounterBufferAddress = vkGetBufferDeviceAddress(engine->_device, &addressInfo);

	//batchoffset buffer
	float dataSize = chunkSystem->indirect_buffer_size * sizeof(glm::vec4);
	chunkSystem->_meshBuffers[i].batchOffsetBuffer = engine->create_buffer(dataSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	addressInfo.buffer = chunkSystem->_meshBuffers[i].batchOffsetBuffer.buffer;
	chunkSystem->_meshBuffers[i].batchOffsetBufferAddress = vkGetBufferDeviceAddress(engine->_device, &addressInfo);

	//finalIndirect buffer
	chunkSystem->_meshBuffers[i].finalIndirectBuffer = engine->create_buffer(sizeof(VkDrawIndexedIndirectCommand) * chunkSystem->indirect_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
	addressInfo.buffer = chunkSystem->_meshBuffers[i].finalIndirectBuffer.buffer;
	chunkSystem->_meshBuffers[i].finalIndirectBufferAddress = vkGetBufferDeviceAddress(engine->_device, &addressInfo);

	engine->_mainDeletionQueue.push_function([=](){
	    engine->destroy_buffer(chunkSystem->_meshBuffers[i].batchOffsetBuffer);
	    engine->destroy_buffer(chunkSystem->_meshBuffers[i].drawCounterBuffer);
	    engine->destroy_buffer(chunkSystem->_meshBuffers[i].finalIndirectBuffer);
	});
    }
}

GPUCullData CullComputePipeline::cull_unifrom_data(GPUSceneData sceneData, ChunkSystem* chunkSystem, VkExtent2D* _windowExtent){
    //임시로 fovY를 저장을 한다.
    float fovY = 90.0;
    float aspect = (float)_windowExtent->width / (float)_windowExtent->height;
    float tanHalfFov = tan(glm::radians(fovY * 0.5f));

    GPUCullData uniformData;
    uniformData.view = sceneData.view;

    uniformData.frustum.x = 1.0f / sqrt(1.0f + (tanHalfFov * aspect) * (tanHalfFov * aspect));
    uniformData.frustum.y = (tanHalfFov * aspect) * uniformData.frustum[0];
    uniformData.frustum.z = 1.0f / sqrt(1.0f + tanHalfFov * tanHalfFov);
    uniformData.frustum.w = tanHalfFov * uniformData.frustum[2];

    uniformData.znear = 0.01f;
    uniformData.zfar = 1000.0f;           
    uniformData.drawCount = chunkSystem->indirect_buffer_size;          
    uniformData.cullingEnabled = 1;      
    uniformData.occlusionEnabled = 0;    
    uniformData.lodEnabled = 0;         
    uniformData.pyramidWidth = 0;          
    uniformData.pyramidHeight = 0;        

    return uniformData;
}

void CullComputePipeline::cull_compute_descriptor(VulkanEngine* engine, CullUniformBufferData* cullUniformBuffer, GPUSceneData sceneData, ChunkSystem* chunkSystem, VkExtent2D* _windowExtent){
    cullUniformBuffer->cullShaderDescriptor = engine->globalDescriptorAllocator.allocate(engine->_device, cullUniformBuffer->cullShaderDescriptorLayout);
    cullUniformBuffer->cullShaderBuffer = engine->create_buffer(sizeof(GPUCullData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    DescriptorWriter writer;
    GPUCullData* cullUniformData = nullptr;
    vmaMapMemory(engine->_allocator, cullUniformBuffer->cullShaderBuffer.allocation, (void**)&cullUniformData);
    //여기에다가 너가 원하는 데이터를 알아서 넣으세요
    *cullUniformData = cull_unifrom_data(sceneData, chunkSystem, _windowExtent);
    vmaUnmapMemory(engine->_allocator, cullUniformBuffer->cullShaderBuffer.allocation);

    writer.write_buffer(0, cullUniformBuffer->cullShaderBuffer.buffer, sizeof(GPUCullData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(engine->_device, cullUniformBuffer->cullShaderDescriptor);

    // cullUniformBuffer 객체/포인터가 나중에 소멸하더라도,
    // 해제에 필요한 핸들 정보(AllocatedBuffer 구조체 값) 자체를 복사해서 람다 내부에 안전하게 보관. 핸들은 그냥 VkBuffer이 호텔방 혹은 사물함이라고 가정 하면 handle은 번호표 혹은 키다.
    // 이러면 핸들값 통로가 있기 때문에 cullUniformBuffer의 주소가 없어도 안전하게 해재 가능
    // 나중에 이거는 지우고 velog에다가 넣으삼
    AllocatedBuffer bufferToDestroy = cullUniformBuffer->cullShaderBuffer;
    engine->_mainDeletionQueue.push_function([=]() {
	engine->destroy_buffer(bufferToDestroy);
    });
}

void CullComputePipeline::init_cullcompute_pipeline(VulkanEngine* engine, VkPipelineLayout* _cullPipelineLayout, CullComputePipelineData* cullCompute, CullUniformBufferData* cullUniformBuffer){
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;

    //UBO사용
    VkDescriptorSetLayout layout = engine->cullUniformBuffer.cullShaderDescriptorLayout;
    computeLayout.pSetLayouts = &layout;
    computeLayout.setLayoutCount = 1;

    //푸쉬컨스턴트도 쓸지는 아직 모르겠다(이제 필요하니 켜라 ㅇㅇ)
    VkPushConstantRange pushConstant{};
    //시작점
    pushConstant.offset = 0;
    //그냥 전체적인 사이즈 (ComputePushConstants구조체(engine.h))
    pushConstant.size = sizeof(CullComputePushConstant);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    //이제 단순히 디스크립터 뿐만 아닌 pushConstant도 추가한것이다
    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(engine->_device, &computeLayout, nullptr, _cullPipelineLayout));

    VkShaderModule compShader;
    if(!vkutil::load_shader_module("./spv/cull.comp.spv", engine->_device, &compShader)){
	fmt::print("Error when building the voxel compute shader module");
    }

    //shader상세 정보들
    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    //실제 쉐이더 가져와서 넣는다
    stageinfo.module = compShader;
    stageinfo.pName = "main";

    //이제 pipeline layout긜고 쉐이더 상새정보(stageinfo)연결
    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = *_cullPipelineLayout;
    computePipelineCreateInfo.stage = stageinfo;

    //이제 compute shader를 실제로 가지고 온다 ㅇㅇ.
    cullCompute->layout = *_cullPipelineLayout;
    cullCompute->name = "voxel_data";
    VK_CHECK(vkCreateComputePipelines(engine->_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &cullCompute->pipeline));
    vkDestroyShaderModule(engine->_device, compShader, nullptr);

    engine->_mainDeletionQueue.push_function([=,this](){
	vkDestroyPipelineLayout(engine->_device, *_cullPipelineLayout, nullptr);
	vkDestroyPipeline(engine->_device, cullCompute->pipeline, nullptr);
    });
}
