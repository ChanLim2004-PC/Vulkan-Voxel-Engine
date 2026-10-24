#include "../../../header/voxel_compute_pipeline.h"
#include "../../../header/vk_pipeline.h"
#include "../../../header/vk_engine.h"

void VoxelPipeline::init_prefix_pipeline(VulkanEngine* engine, VkPipelineLayout* pipelineLayout, VoxelComputePipeline* pipeline){
    VkPipelineLayoutCreateInfo prefixPipelineLayout{};
    prefixPipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    prefixPipelineLayout.pNext = nullptr;

    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(PrefixPushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    prefixPipelineLayout.pPushConstantRanges = &pushConstant;
    prefixPipelineLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(engine->_device, &prefixPipelineLayout, nullptr, pipelineLayout));

    VkShaderModule prefix_comp;
    if(!vkutil::load_shader_module("./spv/prefix_sum.comp.spv", engine->_device, &prefix_comp)){
	fmt::print("Error when building the voxel compute shader module");
    }

    //shader상세 정보들
    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    //실제 쉐이더 가져와서 넣는다
    stageinfo.module = prefix_comp;
    stageinfo.pName = "main";

    //이제 pipeline layout긜고 쉐이더 상새정보(stageinfo)연결
    VkComputePipelineCreateInfo prefixPiepleinCreateInfo{};
    prefixPiepleinCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    prefixPiepleinCreateInfo.pNext = nullptr;
    prefixPiepleinCreateInfo.layout = *pipelineLayout;
    prefixPiepleinCreateInfo.stage = stageinfo;

    //이제 compute shader를 실제로 가지고 온다 ㅇㅇ.
    pipeline->layout = *pipelineLayout;
    pipeline->name = "voxel_count_pipeline";
    VK_CHECK(vkCreateComputePipelines(engine->_device,VK_NULL_HANDLE,1,&prefixPiepleinCreateInfo, nullptr, &pipeline->pipeline));
    vkDestroyShaderModule(engine->_device, prefix_comp, nullptr);

    engine->_mainDeletionQueue.push_function([=,this](){
	vkDestroyPipelineLayout(engine->_device, pipeline->layout, nullptr);
	vkDestroyPipeline(engine->_device, pipeline->pipeline, nullptr);
    });
}
