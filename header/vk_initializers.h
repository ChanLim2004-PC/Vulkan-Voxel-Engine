#pragma once
#include <vulkan/vulkan.h>

//설명서 즉 info를 만들어줒는 곳
namespace vkinit {
// 커맨드 풀 설명서 뽑아주는 함수 선언
VkCommandPoolCreateInfo command_pool_create_info(
    uint32_t queueFamilyIndex, 
    VkCommandPoolCreateFlags flags = 0);

// 커맨드 버퍼 설명서 뽑아주는 함수 선언
VkCommandBufferAllocateInfo command_buffer_allocate_info(
    VkCommandPool pool, 
    uint32_t count = 1);

VkFenceCreateInfo fence_create_info(
    VkFenceCreateFlags flags);

VkSemaphoreCreateInfo semaphore_create_info(
    VkSemaphoreCreateFlags flags = 0);

VkCommandBufferBeginInfo command_buffer_begin_info(
    VkSemaphoreCreateFlags flags = 0);

VkImageSubresourceRange image_subresource_range(
    VkImageAspectFlags aspectMask = 0);

VkSemaphoreSubmitInfo semaphore_submit_info(
    VkPipelineStageFlags2 stageMask = 0, VkSemaphore semaphore = 0);

VkCommandBufferSubmitInfo command_buffer_submit_info(
    VkCommandBuffer cmd);

VkSubmitInfo2 submit_info(
    VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo);

VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);

VkImageViewCreateInfo imageview_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);

VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue* clear ,VkImageLayout layout);

VkRenderingAttachmentInfo depth_attachment_info(VkImageView view, VkImageLayout layout);

VkRenderingInfo rendering_info(VkExtent2D _swapchainExtent, VkRenderingAttachmentInfo* colorAttachment, VkRenderingAttachmentInfo* depthAttachment = 0);

VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info(VkShaderStageFlagBits stage, VkShaderModule module);

VkPipelineLayoutCreateInfo pipeline_layout_create_info();
}
