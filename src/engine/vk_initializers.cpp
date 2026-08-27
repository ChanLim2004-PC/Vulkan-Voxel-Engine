#include "../../header/vk_initializers.h"
#include <cstddef>

//commandPool Info를 집접 만들어주는 곳
VkCommandPoolCreateInfo vkinit::command_pool_create_info(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags) {
    VkCommandPoolCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.queueFamilyIndex = queueFamilyIndex;
    info.flags = flags;
    return info;
}

//여기도 비슷한 내용(command Buffer Info)
VkCommandBufferAllocateInfo vkinit::command_buffer_allocate_info(VkCommandPool pool, uint32_t count) {
    VkCommandBufferAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = pool;
    info.commandBufferCount = count;
    //바로 Graphics Queue로 보내개끔
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    return info;
}

VkFenceCreateInfo vkinit::fence_create_info(VkFenceCreateFlags flags /*= 0*/)
{
    VkFenceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = flags;
    return info;
}

VkSemaphoreCreateInfo vkinit::semaphore_create_info(VkSemaphoreCreateFlags flags /*= 0*/)
{
    VkSemaphoreCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = flags;
    return info;
}

VkCommandBufferBeginInfo vkinit::command_buffer_begin_info(VkCommandBufferUsageFlags flags /*= 0*/)
{
    VkCommandBufferBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext = nullptr;

    info.pInheritanceInfo = nullptr;
    info.flags = flags;
    return info;
}

VkImageSubresourceRange vkinit::image_subresource_range(VkImageAspectFlags aspectMask)
{
    VkImageSubresourceRange subImage {};
    subImage.aspectMask = aspectMask;
    subImage.baseMipLevel = 0;
    subImage.levelCount = VK_REMAINING_MIP_LEVELS;
    subImage.baseArrayLayer = 0;
    subImage.layerCount = VK_REMAINING_ARRAY_LAYERS;

    return subImage;
}

VkSemaphoreSubmitInfo vkinit::semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore)
{
    VkSemaphoreSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    //실제 semaphore 넣기
    submitInfo.semaphore = semaphore;
    //정확이 어떤 파이프라인 단계에서 작동하는지
    submitInfo.stageMask = stageMask;
    submitInfo.deviceIndex = 0;
    submitInfo.value = 1;

    return submitInfo;
}

VkCommandBufferSubmitInfo vkinit::command_buffer_submit_info(VkCommandBuffer cmd)
{
    VkCommandBufferSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    info.pNext = nullptr;
    //command buffer 넣는 과정
    info.commandBuffer = cmd;
    info.deviceMask = 0;
    return info;
}

//전체적인 세마포어 혹은 커멘드 버퍼를 얼마나 넣을 것인지. 전체적인 info를 출력을 해준다
VkSubmitInfo2 vkinit::submit_info(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo)
{
    VkSubmitInfo2 info = {};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.pNext = nullptr;

    info.waitSemaphoreInfoCount = waitSemaphoreInfo == nullptr ? 0 : 1;
    info.pWaitSemaphoreInfos = waitSemaphoreInfo;

    info.signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0 : 1;
    info.pSignalSemaphoreInfos = signalSemaphoreInfo;

    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = cmd;

    return info;
}

VkImageCreateInfo vkinit::image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent)
{
    VkImageCreateInfo info = {};
    //이미지 생성
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.pNext = nullptr;

    //이미지는 언제나 2D(당연한 소리)
    info.imageType = VK_IMAGE_TYPE_2D;

    //색상
    info.format = format;
    //크기
    info.extent = extent;

    //이건 아직 안쓴다
    info.mipLevels = 1;
    info.arrayLayers = 1;

    //안티에일리어싱인데 아직은 1로 둘거다
    //for MSAA. we will not be using it by default, so default it to 1 sample per pixel.
    info.samples = VK_SAMPLE_COUNT_1_BIT;

    //GPU가 가장 읽기 좋은 상태(Optimal)로 두겠다
    //optimal tiling, which means the image is stored on the best gpu format
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    //flags는 너가 나중에 보낼거임
    info.usage = usageFlags;

    return info;
}

//이미지 뷰--> 걍 GPU가 읽을 수 있도록 VkImage에다가 attach하는 것이다
VkImageViewCreateInfo vkinit::imageview_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags)
{
    // build a image-view for the depth image to use for rendering
    VkImageViewCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.pNext = nullptr;

    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.image = image;
    info.format = format;
    //mipmap 시작
    info.subresourceRange.baseMipLevel = 0;
    //mipmap 어디까지 보여줄꺼야?? (0~1)
    info.subresourceRange.levelCount = 1;
    //레이어도 비슷한 논리
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    //그냥 평범한 사진 데이터인지 아니면 depth인지 결정
    info.subresourceRange.aspectMask = aspectFlags;

    return info;
}
VkRenderingInfo vkinit::rendering_info(VkExtent2D _swapchainExtent, VkRenderingAttachmentInfo* colorAttachment, VkRenderingAttachmentInfo* depthAttachment){
    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.pNext = nullptr;
    renderInfo.renderArea = { 0, 0, _swapchainExtent.width, _swapchainExtent.height };
    renderInfo.layerCount = 1;
    if (colorAttachment) {
	renderInfo.colorAttachmentCount = 1;
	renderInfo.pColorAttachments = colorAttachment;
    }
    if (depthAttachment) {
	renderInfo.pDepthAttachment = depthAttachment;
    }
    return renderInfo;
}
VkRenderingAttachmentInfo vkinit::attachment_info(VkImageView view, VkClearValue* clear ,VkImageLayout layout){
    //다이네믹 랜더링을 위한 attachemnts들
    VkRenderingAttachmentInfo colorAttachment {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.pNext = nullptr;
    colorAttachment.imageView = view;
    colorAttachment.imageLayout = layout;
    //loadOP --> 다이네믹 랜더링이 처음 시작을 할때 정확이 뭘 해야해??
    //만약 clear이 true이면 VK_ATTACHMENT_LOAD_OP_CLEAR --> Reset the color attachemnts or depth attachemnts
    //VK_ATTACHMENT_LOAD_OP_LOAD --> just bring the previouse 도화지
    colorAttachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    //storeOP끝나고 난 후는?? --> 그냥 끝난 값을 뱉는다
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    //만약 clear이면 colorAttachment안에 clearValue가 있을거임 그걸로 초기화를 시키면 됌
    if (clear) {
        colorAttachment.clearValue = *clear;
    }
    return colorAttachment;
}
VkRenderingAttachmentInfo vkinit::depth_attachment_info(VkImageView view, VkImageLayout layout)
{
    VkRenderingAttachmentInfo depthAttachment {};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.pNext = nullptr;

    //view 및 layout을 일단 저장
    depthAttachment.imageView = view;
    depthAttachment.imageLayout = layout;
    //depth attachemtn를 랜더링 시작 전 일단 비우고 시작
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    //랜더링 후 일단은 저장
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    //새로운 프래임을 그리기 전 비우고 시작 --> 0.0까지 초기화를 시켜줘
    //여기서 알아야 하는것은 한번 초기화를 하면 [0.0, 0.0, 0.0 ...] 각 픽샐은 이렇게 됌
    //이제 여기서 depth test를 할떄 비교를 하는 거임 0.3f >= 0.0f 이므로 적당히 떨어진데에다가 그리는 거임
    //만약 1.0f 라고 생각을 하면 0.3f >= 1.0f는 성립이 안돼므로 안그려버림(이러면 안됌 그려야지)
    //우리가 0.0f를 쓰는 이유는 단수함 (Reversed-Z)를 쎃기 때문이다
    depthAttachment.clearValue.depthStencil.depth = 0.f;

    return depthAttachment;
}
VkPipelineShaderStageCreateInfo vkinit::pipeline_shader_stage_create_info(VkShaderStageFlagBits stage, VkShaderModule module){
    VkPipelineShaderStageCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderInfo.pNext = nullptr;
    shaderInfo.stage = stage;
    shaderInfo.module = module;
    shaderInfo.pName = "main";

    return shaderInfo;
}
VkPipelineLayoutCreateInfo vkinit::pipeline_layout_create_info(){
    VkPipelineLayoutCreateInfo graphicsLayout {};
    graphicsLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    graphicsLayout.pNext = nullptr;
    graphicsLayout.pSetLayouts = nullptr;
    graphicsLayout.setLayoutCount = 0;

    return graphicsLayout;
}
