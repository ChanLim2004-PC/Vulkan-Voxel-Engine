#pragma once
#include "./vk_initializers.h"
#include "./vk_engine.h"
#include <vector>

namespace vkutil {
    bool load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);
}
struct PipelineSetter{
    VkViewport viewport(float windowExtentWidth, float windowExtentHeight);
    VkRect2D scissor(float windowExtentWidth, float windowExtentHeight);
};
class PipelineBuilder {
public:
    std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
    VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
    VkPipelineRasterizationStateCreateInfo _rasterizer;
    VkPipelineColorBlendAttachmentState _colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo _multisampling;
    VkPipelineLayout _pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo _depthStencil;
    VkPipelineRenderingCreateInfo _renderInfo;
    VkFormat _colorAttachmentformat;

    PipelineBuilder(){ clear(); }

    void clear();

    VkPipeline build_pipeline(VkDevice device);
    
    void set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);

    void set_input_topology(VkPrimitiveTopology topology);

    void set_polygon_mode(VkPolygonMode mode);

    void set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace);

    void set_multisampling();

    void blending_additive();
    void blending();

    void set_color_attachment_format(VkFormat format);

    void set_depth_format(VkFormat format);

    void depthtest(bool enable, VkCompareOp op);
};
