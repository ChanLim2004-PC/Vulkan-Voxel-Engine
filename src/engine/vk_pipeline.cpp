#include "../../header/vk_pipeline.h"
#include "../../header/vk_initializers.h"
#include <fmt/core.h>

#include <vector>
#include <fstream>

//그저 쉐이더 파일을 담을 그릇과 그안에 쉐이더를 담는 과정이다
bool vkutil::load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule)
{
    // open the file. With cursor at the end
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        return false;
    }

    // find what the size of the file is by looking up the location of the cursor
    // because the cursor is at the end, it gives the size directly in bytes
    size_t fileSize = (size_t)file.tellg();

    // spirv expects the buffer to be on uint32, so make sure to reserve a int
    // vector big enough for the entire file
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    // put file cursor at beginning
    file.seekg(0);

    // load the entire file into the buffer
    file.read((char*)buffer.data(), fileSize);

    // now that the file is loaded into the buffer, we can close it
    file.close();

    // create a new shader module, using the buffer we loaded
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;

    // codeSize has to be in bytes, so multply the ints in the buffer by size of
    // int to know the real size of the buffer
    createInfo.codeSize = buffer.size() * sizeof(uint32_t);
    createInfo.pCode = buffer.data();

    // check that the creation goes well.
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return false;
    }
    *outShaderModule = shaderModule;
    return true;
}

//파이프라인 클새르 객채에 있는 필드들을 다 리셋을 하고 시작
void PipelineBuilder::clear()
{
    // clear all of the structs we need back to 0 with their correct stype

    _inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };

    _rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };

    _colorBlendAttachment = {};

    _multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };

    _pipelineLayout = {};

    _depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

    _renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

    _shaderStages.clear();
}

//실제 파이프라인을 만드는 과정이다 ㅇㅇ
VkPipeline PipelineBuilder::build_pipeline(VkDevice device)
{
    //ViewPort관련된 INFO (현제는 아무것도 적을 필요가 없다)
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    //blending도 쓸 의도가 없다
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext = nullptr;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    //_colorBlendAttachment --> 이것은 그냥 붙쳐놓는거 (아무 내용도 없다...아직음)
    colorBlending.pAttachments = &_colorBlendAttachment;

    // 여기서는 [x,y,z,r,g,b,index1,index2,....]이 방식을 쓰지 않을 것이다(VBO를 안쓴다)
    VkPipelineVertexInputStateCreateInfo _vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

      // build the actual pipeline
    // we now use all of the info structs we have been writing into into this one
    // to create the pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    // connect the renderInfo to the pNext extension mechanism
    pipelineInfo.pNext = &_renderInfo;

    //쉐이더 부품들의 사이즈
    pipelineInfo.stageCount = (uint32_t)_shaderStages.size();
    //실제 쉐이더 데이터들
    pipelineInfo.pStages = _shaderStages.data();
    //VBO 인포들이긴 하지만 지금은 안씀
    pipelineInfo.pVertexInputState = &_vertexInputInfo;
    //Topology Info(Index Buffer Order)
    pipelineInfo.pInputAssemblyState = &_inputAssembly;
    //사이즈 결정
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &_rasterizer;
    pipelineInfo.pMultisampleState = &_multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDepthStencilState = &_depthStencil;
    pipelineInfo.layout = _pipelineLayout;

    //우리가 랜더링 도중 바꾸고 싶은 부분(이미지 사이즈, 시저)
    VkDynamicState state[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicInfo.pDynamicStates = &state[0];
    dynamicInfo.dynamicStateCount = 2;
    pipelineInfo.pDynamicState = &dynamicInfo;

    //실제 이걸 갖다가 파이프라인 제작
    VkPipeline newPipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline)
        != VK_SUCCESS) {
        fmt::println("failed to create pipeline");
        return VK_NULL_HANDLE; // failed to create graphics pipeline
    } else {
        return newPipeline;
    }
}
//이제 _shaderStages 안에다가 우리가 원하는 쉐이더들 넣기
void PipelineBuilder::set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader)
{
    _shaderStages.clear();

    //쉐이더 vertex 넣기
    _shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));

    //쉐이더 fragemnt 넣기
    _shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
}

void PipelineBuilder::set_input_topology(VkPrimitiveTopology topology)
{
    _inputAssembly.topology = topology;
    // we are not going to use primitive restart on the entire tutorial so leave
    // it on false
    _inputAssembly.primitiveRestartEnable = VK_FALSE;
}

void PipelineBuilder::set_polygon_mode(VkPolygonMode mode)
{
    //다각형을 어떻게 다룰건지
    //그냥 점만 나둘건지 or 선으로 이을건지 or 그냥 다각형 전채를 색칠할건지
    _rasterizer.polygonMode = mode;
    _rasterizer.lineWidth = 1.f;
}

void PipelineBuilder::set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace)
{
    //_rasterizer.depthClampEnable = VK_TRUE;
    _rasterizer.cullMode = cullMode;
    //앞면만 렌더링 그리고 나머지 뒷 부분은 렌더링을 하지 않겠다
    _rasterizer.frontFace = frontFace;
}

//이것은 multisampling관련된건데... 아직은 하지 않겠다
void PipelineBuilder::set_multisampling()
{
    _multisampling.sampleShadingEnable = VK_FALSE;
    // multisampling defaulted to no multisampling (1 sample per pixel)
    _multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    _multisampling.minSampleShading = 1.0f;
    _multisampling.pSampleMask = nullptr;
    // no alpha to coverage either
    _multisampling.alphaToCoverageEnable = VK_FALSE;
    _multisampling.alphaToOneEnable = VK_FALSE;
}
//이것은 그냥 우리 blending을 위해 샊갈을 add하는 것이다
//뭘 하는거냐면 그냥 색깔을 서로 더해서 더 밝게 만드는게 목적이다 뒤에 있는 object들의 샊갈을 유지를 하면서
//"배경은 100% 다 보여주고, 앞에있는 내 물채는 더 밝게"
void PipelineBuilder::blending_additive(){
    //색깔 format
    _colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    _colorBlendAttachment.blendEnable = VK_TRUE;
    //내 투명도는 alpha(w)만큼만
    _colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    //배경 색상은 오로지 유지를 한체
    _colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    //src 그리고 dst를 둘이 더할것이다(이것은 단순한 더하기가 아닌 공식을 쓴다.)
    //최종 색상 = (새로 그릴 색상 * SrcAlpha) + (기존 배경 색상 * 1.0)
    _colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    _colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    _colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    //최종 Alpha = (새 알파 * 1.0) + (기존 알파 * 0.0) = 새알파
    _colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}
//이제 블랜딩도 할것이다(이것은 가정(src--> 앞에 있는 투명한 물체), (dst--> 뒤에 있는 나머지 물체들))
void PipelineBuilder::blending(){
    _colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    _colorBlendAttachment.blendEnable = VK_TRUE;
    //비슷한 논리 --> 내 투명도는 alpha(w)만큼만
    _colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    //배경은 내 투명도를 뺀 만큼만 남기고 (1 - Alpha)
    _colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    //src 그리고 dst를 둘이 더할것이다(이것은 단순한 더하기가 아닌 공식을 쓴다.)
    //최종색상 = (새로 그릴 색상 * SrcAlpha) + (기존 배경 색상 * (1.0 - SrcAlpha))
    _colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    _colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    _colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    _colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void PipelineBuilder::set_color_attachment_format(VkFormat format)
{
    //R8G8B8A8_UNORM 각각 컬러가 몇 비트씩 가져가는지
    _colorAttachmentformat = format;
    // connect the format to the renderInfo  structure
    _renderInfo.colorAttachmentCount = 1;
    _renderInfo.pColorAttachmentFormats = &_colorAttachmentformat;
}

void PipelineBuilder::set_depth_format(VkFormat format)
{
    //아까 _colorAttachmentformat과 비슷한 논리
    _renderInfo.depthAttachmentFormat = format;
}

//이것은 파이프라인에 depthtest를 하겠다
void PipelineBuilder::depthtest(bool enable, VkCompareOp op)
{
    _depthStencil.depthTestEnable = VK_TRUE;
    //깊이 테스트를 통과를 한 픽샐들 대상으로 다시 쓸 수 있도록
    _depthStencil.depthWriteEnable = enable;
    //VK_COMPARE_OP_GREATER_OR_EQUAL (새 값이 기존 값보다 크거나 같으면 합격)
    //우린 reversed-z 형식을 쓰기 때문에 위 플래그를 받아야 한다
    //만약 공식적인 z형식을 쓴다면 다른 플래그를 써야함
    _depthStencil.depthCompareOp = op;
    //특정 depth value만 test를 한다
    //예를 들어 깊이 값이 0.2에서 0.8 사이인 픽셀만 골라서 그려라
    //근데 우리는 0.0이든 1.0이든 그냥 다 하기를 원한다
    _depthStencil.depthBoundsTestEnable = VK_FALSE;
    _depthStencil.stencilTestEnable = VK_FALSE;
    _depthStencil.front = {};
    _depthStencil.back = {};
    _depthStencil.minDepthBounds = 0.f;
    _depthStencil.maxDepthBounds = 1.f;
}

VkViewport PipelineSetter::viewport(float windowExtentWidth, float windowExtentHeight){
    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = windowExtentWidth;
    viewport.height = windowExtentHeight;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;

    return viewport;
}
VkRect2D PipelineSetter::scissor(float windowExtentWidth, float windowExtentHeight){
    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = windowExtentWidth;
    scissor.extent.height = windowExtentHeight;

    return scissor;
}

