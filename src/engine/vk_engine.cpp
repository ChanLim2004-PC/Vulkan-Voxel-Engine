#define GLM_FORCE_DEPTH_ZERO_TO_ONE 
#define VMA_IMPLEMENTATION
#include "fmt/base.h"
#include <iostream>
#include <cstdint>
#include <span>
#include "vk_mem_alloc.h"

#include "../../header/vk_engine.h"
#include "../../header/vk_initializers.h"
#include "../../header/vk_images.h"
#include "../../header/vk_pipeline.h"

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

#include <chrono>
#include <thread>

constexpr bool bUseValidationLayers = true;
//엔진의 주소
VulkanEngine* loadedEngine = nullptr;
//해더 파일 vk_engine.h에 있는 싱글톤 접근 함수 --> 어디에서든 엔진을 접근할 수 있음
VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }

auto lastTime = std::chrono::high_resolution_clock::now();
int frameCount = 0;
auto lastFpsTime = std::chrono::high_resolution_clock::now();

void VulkanEngine::init_vulkan()
{
    vkb::InstanceBuilder builder;

    //make the vulkan instance, with basic debug features
    auto inst_ret = builder.set_app_name("RayTracer")
	//validation 계층 켜기(여담: 나중에 코드가 너무 커지면 오히려 방해여서 퍼포먼스 체크때는 끄삼)
	.request_validation_layers(bUseValidationLayers)
	//디버그는 그냥 VkBootstrap에서 제공을 해주는 평범한 디버거(커멘드에다가 에러 올려줌)
	.use_default_debug_messenger()
	.require_api_version(1, 3, 0)
	.build();
    vkb::Instance vkb_inst = inst_ret.value();

    //grab the instance 
    _instance = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;

    //VkSurface만들기
    SDL_Vulkan_CreateSurface(_window, _instance, nullptr, &_surface);

    //vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features.dynamicRendering = true;
    features.synchronization2 = true;

    //vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;
    features12.drawIndirectCount = true;

    VkPhysicalDeviceFeatures deviceFeatures{};
    //와이어프레임
    deviceFeatures.fillModeNonSolid = VK_TRUE; 
    deviceFeatures.multiDrawIndirect = VK_TRUE;
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .pNext = nullptr,
        .rayQuery = VK_TRUE
    };

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures{
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
	.pNext = nullptr,
	.accelerationStructure = VK_TRUE,
	.accelerationStructureIndirectBuild = VK_TRUE
    };

    //use vkbootstrap to select a gpu. 
    //We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice physicalDevice = selector
	.set_minimum_version(1, 3)
	//우리 physicalDevice는 vulkan 1.2 그리고 1.3 기능들을 쓰겠다
	.set_required_features_13(features)
	.set_required_features_12(features12)
	.set_required_features(deviceFeatures)
	.add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)  
	.add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) 
	.add_required_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)             
	.add_required_extension_features(accelFeatures)
	.add_required_extension_features(rayQueryFeatures)
	.set_surface(_surface)
	.select()
	.value();

    //create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    vkb::Device vkbDevice = deviceBuilder.build().value();

    //use vkbootstrap to get a Graphics queue
    _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // Get the VkDevice handle used in the rest of a vulkan application
    //logical Device--> 그냥 어떤 Vk객채를 만들떄 여기 logical device에서 처리가 돼!
    //이제 클래스에다가 삽입
    _device = vkbDevice.device;
    _chosenGPU = physicalDevice.physical_device;

    //VMA소환
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _chosenGPU;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &_allocator);

    //이거는 deletion queue에다가 넣어서 지우는 과정
    _mainDeletionQueue.push_function([&]() {
	vmaDestroyAllocator(_allocator);
    });
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };

    //그냥 format으로 RGBA를 사용한다 각각 8bit씩
    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
	//.use_default_format_selection()
	.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
	//use vsync present mode-->이것은 그냥 모니터하고 GPU속도를 맞춘다는 의미
	//.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
	.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
	//픽샐 크기
	.set_desired_extent(width, height)
	//"이 스왑체인 도화지에는 직접 붓으로 그림을 그리지 않을 거야. 다른 데서 다 그려진 완성본 이미지를 그대로 복사해 와서 붙여넣을 겉껍데기 목적지로만 쓸 거야!
	.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
	.build()
	.value();

    //값들 넣기
    _swapchainExtent = vkbSwapchain.extent;
    _swapchain = vkbSwapchain.swapchain;
    _swapchainImages = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::init_swapchain()
{
    create_swapchain(_windowExtent.width, _windowExtent.height);
    //이미지 사이즈는 현제 윈도우하고 크기가 똑같음
    VkExtent3D drawImageExtent = {
	_windowExtent.width,
	_windowExtent.height,
	1
    };
    //---------------실제 이미지 만들기------------------------------

    //색깔 및 이미지 크기 결정
    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages{};
    //걍 스왑체인으로 복사가 가능하게 하는 것
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    //화면 리셋용
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    //Compute shader가 eddit (image)를 할수 있겠금
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    //그림을 그릴수 있게금 설정
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    //image info 만드는 과정
    VkImageCreateInfo rimg_info = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

    //for the draw image, we want to allocate it from gpu local memory
    VmaAllocationCreateInfo rimg_allocinfo = {};
    //Vram에다가 메모리 공간을 만들어줘
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    //VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT --> 이것의 의미는 그냥 GPU가 가장 빠릴 접근할수 있는 메모리 VRAM에다가 적용을 해달라 이말이다
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //실제로 할당(공간만 파는거임)
    vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);
    vmaSetAllocationName(_allocator, _drawImage.allocation, "Draw_Image_Allocation");

    //build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

    //-------------------------------------------------------------------------------------------
    
    //---------------------depth 이미지 만들기---------------------------------------------------

    //이미지 포멧 결졍 depth test --> 32bit(float) 공간 만들기
    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    //이미지 사이즈는 윈도우 창 크기 그대로
    _depthImage.imageExtent = drawImageExtent;
    VkImageUsageFlags depthImageUsages{};
    //depth 및 stencil둘다 usage로 쓰기(근데 아직 stencil은 존재하지가 않다 --> D24_UNORM_S8_UINT{stencil Usage} 그래서 그냥 쩔 수 없이 depth만 쓰겠다)
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    //이미지 info 만들기
    VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthImage.imageFormat, depthImageUsages, drawImageExtent);

    //메모리 할당은 위에 이미지하고 똑같은 GPU에서만 사용 가능함
    vmaCreateImage(_allocator, &dimg_info, &rimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr);
    vmaSetAllocationName(_allocator, _depthImage.allocation, "Depth_Image_Allocation"); 

    //이미지 뷰도 함께 생성(depth)전용
    VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
    VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));

    //-------------------------------------------------------------------------------------------

    //add to deletion queues
    _mainDeletionQueue.push_function([=, this]() {
	vkDestroyImageView(_device, _drawImage.imageView, nullptr);
	vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

	vkDestroyImageView(_device, _depthImage.imageView, nullptr);
	vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
    });
}
void VulkanEngine::destroy_swapchain()
{
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);

    // destroy swapchain resources
    for (int i = 0; i < _swapchainImageViews.size(); i++) {

	vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
    }
}

void VulkanEngine::resize_swapchain()
{
    //일단은 GPU가 전 단계 rendering을 할때까지 기다린다.
    vkDeviceWaitIdle(_device);

    //스왑체린을 일단 파괴
    destroy_swapchain();

    //일단은 바뀐 윈도우 사이즈를 가지고 옴
    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    _windowExtent.width = w;
    _windowExtent.height = h;

    create_swapchain(_windowExtent.width, _windowExtent.height);
    _resize_requested = false;
}

void VulkanEngine::init_commands()
{
    //queue는 graphics queue를 쓰고 command buffer를 리샛을 할 수 있는 command pool 만들기
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    for (int i = 0; i < FRAME_OVERLAP; i++) {
	//실제로 만들기 CommandPool
	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));
	//커멘드 버퍼도 비슷한 방식으로 만들어준다(근데 이거는 걍 공간만 만드는 과정이다)
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
    }

    //command pool을 만듬 근데 immediateCommandPool즉 Imgui 관련된 풀이다
    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

    // 커멘드 버퍼 공간을 파줌(Allocate) _immCommandPool 안에서
    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1);

    //실제 커멘드 버퍼가 살 공간만 생성을 해준다
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

    _mainDeletionQueue.push_function([this]() { 
	vkDestroyCommandPool(_device, _immCommandPool, nullptr);
    });
}

void VulkanEngine::init_sync_structures()
{
    swapchainImageCount = _swapchainImages.size();
    _renderSemaphores.resize(swapchainImageCount);
    //semaphore 긜고 fence에 대한 info
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();
    for(int i = 0; i < swapchainImageCount; i++){
	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderSemaphores[i]));
    }

    //이것은 실제로 만드는 과정
    for (int i = 0; i < FRAME_OVERLAP; i++) {
	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));
	//세마포어는 2개다 하나는 스왑체인 새마포어 또다른 하나는 프리젠트 세마포어
	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
    }
    //이것은 fence를 만드는 과정인데 (immediate을 위한것 즉 dear Imgui를 위한 팬스이다)
    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
    _mainDeletionQueue.push_function([this]() { vkDestroyFence(_device, _immFence, nullptr); });
}

void VulkanEngine::init_descriptors()
{
    //디스크립터 풀 1개 / 디스크립터 세트 10개 만들기 그리고 각각 세트에는 1개의 이미지
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes ={
	{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
	{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
	{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1}
    };
    globalDescriptorAllocator.init(_device, 10, sizes);

    //make the descriptor set layout for our compute draw
    {
	DescriptorLayoutBuilder builder;
	//layout을 만드는 과정인데 binding 0에다가 할거임
	builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	//깔금하게 이제 만들기 layout을
	_drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    {
	DescriptorLayoutBuilder builder;
	//우리가 만드는 버퍼는 IMAGE및 SAMPLER둘다 가지고 노는 버퍼다
	builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	//layout만들기 (프라그먼트 쉐이더 전용이다)
	_singleImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    {
	DescriptorLayoutBuilder builder;
	builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	_gpuSceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    {
	//3번째에다가 바인딩을 하겠다 세트는 1번에다가 하고 _voxelModelDescriptorLayout을 따라가다보면 나옴
	DescriptorLayoutBuilder builder;
	builder.add_binding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	_voxelModelDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    {
	DescriptorLayoutBuilder builder;
	builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	_marchingCubeDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    {
	DescriptorLayoutBuilder builder;
	builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	_fragmentShaderDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    {
	DescriptorLayoutBuilder builder;
	builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	cullUniformBuffer.cullShaderDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    {
	DescriptorLayoutBuilder builder;
	builder.add_binding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
	rayTracingBuffer.tlsaBufferDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    {
	DescriptorLayoutBuilder builder;
	builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	rayTracingUniformBuffer.RayTracerDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    //디스크립터 공간을 하나 만들어줘
    _drawImageDescriptors = globalDescriptorAllocator.allocate(_device,_drawImageDescriptorLayout);	

    DescriptorWriter writer;
    writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    //디스크립터 업데이트 (이제 디스크립터 안에 이미지가 있음)
    writer.update_set(_device, _drawImageDescriptors);

    for (int i = 0; i < FRAME_OVERLAP; i++) {
	// 하는것--> 이것은 그냥 PoolSizeRatio라는 스트럭트에다가 정보를 넣는거임
	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = { 
	    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
	    //{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
	    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 },
	    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
	    {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1}
	};

	//디스크립터에다가 우리가 vk_descriptor.h에서 명시한 struct를 넣어줘
	_frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
	//1000개의 descriptor sets를 담을 수 있겠금 설계를 해놔(여기서 pool까지 다 만들어줌)
	_frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);

	//지우는걸 잊지 말고
	_mainDeletionQueue.push_function([&, i]() {
	    _frames[i]._frameDescriptors.destroy_pools(_device);
	});
    }

    //make sure both the descriptor allocator and the new layout get cleaned up properly
    _mainDeletionQueue.push_function([&]() {
	globalDescriptorAllocator.destroy_pools(_device);
	vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(_device, _singleImageDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(_device, _gpuSceneDataDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(_device, _voxelModelDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(_device, _marchingCubeDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(_device, _fragmentShaderDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(_device, cullUniformBuffer.cullShaderDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(_device, rayTracingBuffer.tlsaBufferDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(_device, rayTracingUniformBuffer.RayTracerDescriptorLayout, nullptr);
    });
}

void VulkanEngine::init_pipelines()
{
    init_background_pipelines();
    init_graphics_pipeline();
    init_compute_pipeline();
    voxelPipeline.init_voxel_count_pipeline(this, &_voxelCountPipeline.layout, &_voxelCountPipeline);
    voxelPipeline.init_prefix_pipeline(this, &_prefixPipeline.layout, &_prefixPipeline);
    cullComputePipeline.init_cullcompute_pipeline(this, &_cullPipelineLayout, &cullCompute, &cullUniformBuffer);
    metalRoughMaterial.build_pipelines(this);
}

void VulkanEngine::init_background_pipelines(){
    //파이프라인 레이아웃
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    //디스크립터layout(디스크립터 블루 프린트만 붙치면 끝이다.)
    computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
    //디스크립터 레이아웃은 딱 하나만 필요하다
    computeLayout.setLayoutCount = 1;

    VkPushConstantRange pushConstant{};
    //시작점
    pushConstant.offset = 0;
    //그냥 전체적인 사이즈 (ComputePushConstants구조체(engine.h))
    pushConstant.size = sizeof(ComputePushConstants) ;
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    //이제 단순히 디스크립터 뿐만 아닌 pushConstant도 추가한것이다
    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

    //쉐이더 파일을 실제로 빌드를 하는 과정이다
    VkShaderModule computeDrawShader;
    if (!vkutil::load_shader_module("./spv/gradient.comp.spv", _device, &computeDrawShader))
    {
	fmt::print("Error when building the compute shader(gradient) \n");
    }

    VkShaderModule colorDrawShader;
    if (!vkutil::load_shader_module("./spv/gradient_color.comp.spv", _device, &colorDrawShader))
    {
	fmt::print("Error when building the compute shader(gradient_color) \n");
    }

    //쉐이더 상새 정보
    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    //실제 쉐이더 가져와서 넣는다
    stageinfo.module = computeDrawShader;
    stageinfo.pName = "main";

    //이제 pipeline layout긜고 쉐이더 상새정보(stageinfo)연결
    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = _gradientPipelineLayout;
    computePipelineCreateInfo.stage = stageinfo;

    ComputeEffect gradient;
    gradient.layout = _gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.data = {};
    VK_CHECK(vkCreateComputePipelines(_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &gradient.pipeline));
    
    //change the pipeline only to Create colorDrawShader
    computePipelineCreateInfo.stage.module = colorDrawShader;

    ComputeEffect gradient_color;
    gradient_color.layout = _gradientPipelineLayout;
    gradient_color.name = "gradient_color";
    gradient_color.data = {};
    gradient_color.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient_color.data.data2 = glm::vec4(0, 0, 1, 1);
    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient_color.pipeline));

    backgroundEffects.push_back(gradient);
    backgroundEffects.push_back(gradient_color);

    vkDestroyShaderModule(_device, computeDrawShader, nullptr);
    vkDestroyShaderModule(_device, colorDrawShader, nullptr);
    _mainDeletionQueue.push_function([=,this]() {
	//어짜피 파이프라인은 똑같은거 씀
	vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
	vkDestroyPipeline(_device, gradient.pipeline, nullptr);
	vkDestroyPipeline(_device, gradient_color.pipeline, nullptr);
    });
}
void VulkanEngine::compute_descriptor(){
    //initiate data-> memcpy happens here
    marchingCubeData.mc_data();

    _marchingCubeDescriptor = globalDescriptorAllocator.allocate(_device, _marchingCubeDescriptorLayout);

    _marchingCubeBuffer = create_buffer(sizeof(MarchingCubeBuffer), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    DescriptorWriter writer;
    MarchingCubeBuffer* sceneUniformData = (MarchingCubeBuffer*)_marchingCubeBuffer.allocation->GetMappedData();
    *sceneUniformData = marchingCubeData.cpuMarchData;

    writer.write_buffer(0, _marchingCubeBuffer.buffer, sizeof(MarchingCubeBuffer), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_device, _marchingCubeDescriptor);

    _mainDeletionQueue.push_function([=,this]() {
	destroy_buffer(_marchingCubeBuffer);
    });
}
void VulkanEngine::init_compute_pipeline(){
    //파이프라인 레이아웃(근데 현제 아무런 디스크립터가 필요가 없다 아쉽게도
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;

    VkDescriptorSetLayout layout = _marchingCubeDescriptorLayout;
    computeLayout.pSetLayouts = &layout;
    computeLayout.setLayoutCount = 1;

    VkPushConstantRange pushConstant{};
    //시작점
    pushConstant.offset = 0;
    //그냥 전체적인 사이즈 (ComputePushConstants구조체(engine.h))
    pushConstant.size = sizeof(GPUDrawPushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    //이제 단순히 디스크립터 뿐만 아닌 pushConstant도 추가한것이다
    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;
    VkShaderModule compShader = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_voxeldataPipelineLayout));

    if(!vkutil::load_shader_module("./spv/voxel.comp.spv", _device, &compShader)){
	fmt::print("Error when building the voxel compute shader module");
	return;
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
    computePipelineCreateInfo.layout = _voxeldataPipelineLayout;
    computePipelineCreateInfo.stage = stageinfo;

    //이제 compute shader를 실제로 가지고 온다 ㅇㅇ.
    voxelComputeEffect.layout = _voxeldataPipelineLayout;
    voxelComputeEffect.name = "voxel_data";

    vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &voxelComputeEffect.pipeline);

    vkDestroyShaderModule(_device, compShader, nullptr);
    _mainDeletionQueue.push_function([=,this](){
	vkDestroyPipelineLayout(_device, _voxeldataPipelineLayout, nullptr);
	vkDestroyPipeline(_device, voxelComputeEffect.pipeline, nullptr);
    });
}
void VulkanEngine::init_graphics_pipeline(){
    VkShaderModule fragShader;
    if (!vkutil::load_shader_module("./spv/shader.frag.spv", _device, &fragShader)) {
	fmt::print("Error when building the triangle fragment shader module");
    }

    VkShaderModule meshShader;
    if (!vkutil::load_shader_module("./spv/tex_image.frag.spv", _device, &meshShader)) {
	    fmt::print("Error when building the fragment shader \n");
    }

    VkShaderModule vertShader;
    if (!vkutil::load_shader_module("./spv/shader.vert.spv", _device, &vertShader)) {
	fmt::print("Error when building the triangle vertex shader module");
    }

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    //무엇을 쓸것이냐?? 그냥 struct를 쓸거임 이번에는 디스크립터 데신
    //스트럭트 안에는 (VkDeviceAddress, 그리고 glm::mat4 이 들어있다, 이것을 pushConstant로 GPU로 넘길것이다.
    bufferRange.size = sizeof(DrawMeshBuffer);
    //오로지 버택스 쉐이더에서만 열것이다
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &bufferRange;
    pipeline_layout_info.pushConstantRangeCount = 1;

    VkDescriptorSetLayout layouts[] = { 
	_singleImageDescriptorLayout,
	_voxelModelDescriptorLayout, 
	_fragmentShaderDescriptorLayout, 
	rayTracingUniformBuffer.RayTracerDescriptorLayout,
	rayTracingBuffer.tlsaBufferDescriptorLayout,
    };

    pipeline_layout_info.pSetLayouts = layouts;
    pipeline_layout_info.setLayoutCount = 5;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_graphicsPipelineLayout));

    PipelineBuilder pipelineBuilder;
    //레이아웃은 우리가 위에서 만든 grpahicsPipelineLayout
    pipelineBuilder._pipelineLayout = _graphicsPipelineLayout;
    //쉐이더를 연결
    pipelineBuilder.set_shaders(vertShader, fragShader);
    //그림 그리기
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    //그림 아웃라인 안에까지 전부다 샊갈로 꽉채운다
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    //no backface culling
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
    //no multisampling
    pipelineBuilder.set_multisampling();
    //yes blending
    pipelineBuilder.blending();
    //pipelineBuilder.blending_additive();
    //no depth testing
    pipelineBuilder.depthtest(true,VK_COMPARE_OP_GREATER_OR_EQUAL);

    //이미지 포멧--> 어떤 샊갈 비트를 쓸껀지 R8G8B8A8
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

    //파이프라인 빌드하기
    _graphicsPipeline = pipelineBuilder.build_pipeline(_device);

    //clean structures
    vkDestroyShaderModule(_device, fragShader, nullptr);
    vkDestroyShaderModule(_device, vertShader, nullptr);
    vkDestroyShaderModule(_device, meshShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
	vkDestroyPipelineLayout(_device, _graphicsPipelineLayout, nullptr);
	vkDestroyPipeline(_device, _graphicsPipeline, nullptr);
    });
}
void GLTFMetallic_Roughness::build_pipelines(VulkanEngine* engine){
    VkShaderModule meshFragShader;
    if (!vkutil::load_shader_module("./spv/mesh.frag.spv", engine->_device, &meshFragShader)) {
	fmt::println("Error when building the triangle fragment shader module");
    }

    VkShaderModule meshVertexShader;
    if (!vkutil::load_shader_module("./spv/mesh.vert.spv", engine->_device, &meshVertexShader)) {
	fmt::println("Error when building the triangle vertex shader module");
    }

    //push constant data들은 전부다 Vertex Shader에서 쓰인다는 소리
    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(DrawMeshBuffer);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    //총 3개의 바인딩이 일어날것이다(디스크립터를 이용을 해서)
    DescriptorLayoutBuilder layoutBuilder;
    //유니폼 버퍼 한개(MaterialResources가 들어간다)--> factors관련 
    layoutBuilder.add_binding(0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    //나머지 2개는 이미지 샘플러
    //colorImage (기본 알베도/색상 텍스처) + colorSampler
    layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    //metalRoughImage (금속성/거칠기 정보가 담긴 맵) + metalRoughSampler
    layoutBuilder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    //디스크립터 레이아웃 만들기
    materialLayout = layoutBuilder.build(engine->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    //총 2가지 layout을 만들거이다 하나는 _gpuSceneDataDescriptorLayout --> 유니폼 버퍼, 다른 하나는 샘플러 materialLayout
    VkDescriptorSetLayout layouts[] = { engine->_gpuSceneDataDescriptorLayout,
	materialLayout };

    //이제 디스크립터는 끝났고 파이프라인 레이아웃(파이프라인에 어떤 통로들이 연결이 될껀지) --> 디스크립터?? 푸쉬 컨스턴트??
    VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
    //우리 디스크립터들 연결
    mesh_layout_info.setLayoutCount = 2;
    mesh_layout_info.pSetLayouts = layouts;
    //푸쉬 컨스턴트 도 연결
    mesh_layout_info.pPushConstantRanges = &matrixRange;
    mesh_layout_info.pushConstantRangeCount = 1;

    VkPipelineLayout newLayout;
    VK_CHECK(vkCreatePipelineLayout(engine->_device, &mesh_layout_info, nullptr, &newLayout));

    opaquePipeline.layout = newLayout;
    transparentPipeline.layout = newLayout;

    //걍 파이프라인 빌드 하는거임 그거 이상은 없음
    PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(meshVertexShader, meshFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipelineBuilder.set_multisampling();
    pipelineBuilder.blending();
    pipelineBuilder.depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    //렌더링 포멧 (R8G8B8A8 인지 D32인지 결정)
    pipelineBuilder.set_color_attachment_format(engine->_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(engine->_depthImage.imageFormat);

    //파이프라인 레이아웃은 방금 만든걸로 설정
    pipelineBuilder._pipelineLayout = newLayout;

    //방금 만든 pipelineBuilder Info들을 이제 넘겨줌 파이프라인 굽게
    opaquePipeline.pipeline = pipelineBuilder.build_pipeline(engine->_device);

    // 여기서는 transparent하게 만들거기 때문에 blending_additive를 사용
    pipelineBuilder.blending_additive();
    //depthTest는 따록 안할거임
    pipelineBuilder.depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
    transparentPipeline.pipeline = pipelineBuilder.build_pipeline(engine->_device);

    vkDestroyShaderModule(engine->_device, meshFragShader, nullptr);
    vkDestroyShaderModule(engine->_device, meshVertexShader, nullptr);

    engine->_mainDeletionQueue.push_function([=,this]() {
	vkDestroyPipeline(engine->_device, opaquePipeline.pipeline, nullptr);
	vkDestroyPipeline(engine->_device, transparentPipeline.pipeline, nullptr);
	vkDestroyPipelineLayout(engine->_device, opaquePipeline.layout, nullptr);
	vkDestroyDescriptorSetLayout(engine->_device, materialLayout, nullptr);
    });
    //clean structures
}
//버퍼 write 과정 하지만 우리 (GLTF Pipeline)위한것이다
MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator)
{
    //여기서는 실제로 transparentPipeline 혹은 opaquePipeline를 둘이 연결하는 과정이다
    MaterialInstance matData;
    matData.passType = pass;
    if (pass == MaterialPass::Transparent) {
	matData.pipeline = &transparentPipeline;
    }
    else {
	matData.pipeline = &opaquePipeline;
    }

    //대충 모든 디스크립터 (gltf)관련 메모리 파기
    matData.materialSet = descriptorAllocator.allocate(device, materialLayout);

    //write및 업데이트
    writer.clear();
    writer.write_buffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_image(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(2, resources.metalRoughImage.imageView, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.update_set(device, matData.materialSet);
    return matData;
}
AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    // 버퍼 그 자체
    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;

    //버퍼를 어디에다가 넣을 것이냐??
    VmaAllocationCreateInfo vmaallocInfo = {};
    //어디다가 공간을 팔것이냐(GPU 아니면 CPU)
    vmaallocInfo.usage = memoryUsage;
    //CPU가 GPU data 포인터로 바로 보낼 수 있다 
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    AllocatedBuffer newBuffer;

    // 이제 실제로 공간을 파는 것이다
    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,
			     &newBuffer.info));
    return newBuffer;
}

//택스처 관련된 함수 (텍스처 실제로 만들기)
AllocatedImage VulkanEngine::create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    AllocatedImage newImage;
    newImage.imageFormat = format;
    newImage.imageExtent = size;


    //이미지 info 가지고 오기
    VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
    if (mipmapped) {
	//만약 mipmap이 켜져있으면 그것만큼 mipmap을 만들어주삼
	img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    // 걍 메모리 할당 과정
    VmaAllocationCreateInfo allocinfo = {};
    allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vmaCreateImage(_allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

    //format은 왜만하면 그냥 평범한 이미지
    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    //근데 format이 depth를 쓰기를 원한다면 depth를 쓰게 만들기
    if (format == VK_FORMAT_D32_SFLOAT) {
	aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    // 이미지 뷰 만들기
    VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, newImage.image, aspectFlag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;
    VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

    //image view, 실제 image 정보, 그리고 메모리할당 총 3개만 하고 넘겨주는 거임
    return newImage;
}
//create_image똑같은 함수이지만 파라미터가 다름
//여기서는 실제 이미지를 옮기는 과정임
AllocatedImage VulkanEngine::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    //이미지 사이즈(데이터 기준)
    size_t data_size = size.depth * size.width * size.height * 4;
    //스테이징 버퍼
    AllocatedBuffer staging_buffer = create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    //이제 진짜로 copy가 일어남(memcpy)
    memcpy(staging_buffer.info.pMappedData, data, data_size);

    //방금 위에서 만든 각종 이미지 규격 및 정보들(이미지 공간, 이미지 뷰)
    AllocatedImage new_image = create_image(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

    //immediate_submit --> 하는일은 스테이징 버퍼에 있는 정보들을 transfer를 통해 옮기는 거임 GPU 이미지 데이터를 할당한 VkImage로
    immediate_submit([&](VkCommandBuffer cmd) {
	vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkBufferImageCopy copyRegion = {};
	copyRegion.bufferOffset = 0;
	copyRegion.bufferRowLength = 0;
	copyRegion.bufferImageHeight = 0;

	//지금은 aspectMask가 이미지 파일만 받을 수 있도록 하드 코딩을 했지만 나중에는 depth image도 받을 수 있도록 너가 알아서 잘 바꿔줘.
	copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.imageSubresource.mipLevel = 0;
	copyRegion.imageSubresource.baseArrayLayer = 0;
	copyRegion.imageSubresource.layerCount = 1;
	copyRegion.imageExtent = size;

	// stagin buffer안에 있는 정보들을 new_image.image(실제 VkImage)에다가 옮기기
	vkCmdCopyBufferToImage(cmd, staging_buffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
			&copyRegion);
	//만약에 mipmap이 있는 이미지라고 우리가 가정을 해보자
	if (mipmapped) {
	    //이미지 mipmap을 만들어줌
            vkutil::generate_mipmaps(cmd, new_image.image,VkExtent2D{new_image.imageExtent.width,new_image.imageExtent.height});
        } else {
	    //만약 아니면 그냥 바로 쉐이더가 읽을 수 있겠금 transition을 진해을 해주면 됌
            vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    });

    destroy_buffer(staging_buffer);

    return new_image;
}
void VulkanEngine::destroy_image(const AllocatedImage& img){
    vkDestroyImageView(_device, img.imageView, nullptr);
    vmaDestroyImage(_allocator, img.image, img.allocation);
}

GPUMeshBuffers VulkanEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers newSurface;

    //create vertex buffer
    //버퍼 만드는데 플래그 자체는 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT--> 이유?? SSBO가 에초에 우리 Vertex data를 넣을거기 때문에
    //그리고 우리 VMA memoryUsage는 GPU에서만 쓰는거임
    newSurface.vertexBuffer = create_buffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    //vertex buffer주소 찾기
    VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertexBuffer.buffer };
    newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

    //인덱스 버퍼 만들기 그리고 계념은 위에 vertexBuffer 만드는 거하고 똑같음
    newSurface.indexBuffer = create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    //vertex buffer주소 찾기
    VkBufferDeviceAddressInfo IndexdeviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.indexBuffer.buffer };
    newSurface.indexBufferAddress = vkGetBufferDeviceAddress(_device, &IndexdeviceAdressInfo);

    //CPU가 임시적으로 데이터를 GPU한테 보내는 버퍼를 staging buffer이라고 부름
    AllocatedBuffer staging = create_buffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    //GetMappedData()--> GPU Vram의 주소를 가지고 오는 함수이다
    //이걸 staging버퍼하고 연결하는 거임
    void* data = staging.allocation->GetMappedData();

    // copy vertex buffer
    memcpy(data, vertices.data(), vertexBufferSize);
    // copy index buffer
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

    //이제 얌전하게 CPU--> GPU 메모리로 복사를 하는거다
    immediate_submit([&](VkCommandBuffer cmd) {
	VkBufferCopy vertexCopy{ 0 };
	vertexCopy.dstOffset = 0;
	vertexCopy.srcOffset = 0;
	vertexCopy.size = vertexBufferSize;

	//실제 복사가 일어나는 곳
	vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

	VkBufferCopy indexCopy{ 0 };
	indexCopy.dstOffset = 0;
	indexCopy.srcOffset = vertexBufferSize;
	indexCopy.size = indexBufferSize;

	vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
    });

    destroy_buffer(staging);

    return newSurface;
}
void VulkanEngine::destroy_buffer(const AllocatedBuffer& buffer)
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}
void VulkanEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    //다이네믹 랜더링 어테치먼트 info
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    //vkCmdBeginRendering에 재출할 다이네믹 랜더링
    VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);
    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}
void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    //일단은 팬스 및 command buffer를 리셋을 해
    VK_CHECK(vkResetFences(_device, 1, &_immFence));
    VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

    //커멘드 버퍼(immediateCommandBuffer가지고 오기
    VkCommandBuffer cmd = _immCommandBuffer;

    //여기서부터 이제 커멘드 버퍼 리코딩을 하는거임
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    //나중에 더 디테일이 밝켜질 것이다
    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    //이제 커멘드버퍼를 submit을 하는거임
    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

    //그래픽스 큐(GPU)에다가 보내면 이제 알아서 execute할거임(만약 GPU에서 queue가 다 끝나면?? _immFence가 on)
    //참고로 그래픽스 큐는 transfer queue도 가능하다... 그래서 나중에 스테이징 버퍼를 에서 VkBuffer로 옮길때는 transfer queue로 작동을 한다
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

    //기다리는 거임 ?? 뭘(_immFence 가 on이면 기다리고 있던 CPU는 이제 다른걸 해도 된다)
    VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}
//dear_imgui 시작
void VulkanEngine::init_imgui()
{
    //IMGUI를 위한 descriptor pool을 생성을 한다
    //일단 공식 사이트 demo에서 가지고 온거임 디테일은 IMGUI공식 사이트에 있다
    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
	{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
	{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
	{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
	{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
	{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
	{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
	{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
	{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
	{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
	{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

    // 2: initialize imgui library
    // this initializes the core structures of imgui
    ImGui::CreateContext();

    // this initializes imgui for SDL
    ImGui_ImplSDL3_InitForVulkan(_window);

    // this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = _instance;
    init_info.PhysicalDevice = _chosenGPU;
    init_info.Device = _device;
    init_info.Queue = _graphicsQueue;
    init_info.DescriptorPool = imguiPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.UseDynamicRendering = true;

    //dynamic rendering parameters for imgui to use
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;

    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);

    // add the destroy the imgui created structures
    _mainDeletionQueue.push_function([=, this]() {
	ImGui_ImplVulkan_Shutdown();
	vkDestroyDescriptorPool(_device, imguiPool, nullptr);
    });
}
void VulkanEngine::init()
{
    //엔진은 오로지 하나만 있어야 하므로
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.-----------
    SDL_Init(SDL_INIT_VIDEO);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    _window = SDL_CreateWindow(
        "Vulkan Engine",
	_windowExtent.width,  
	_windowExtent.height,
	window_flags);
    //-----------------------------------------------------------
    init_vulkan();
    init_swapchain();
    init_commands();
    //Semaphore혹은 Fence관리
    init_sync_structures();
    init_descriptors();	

    //파이프라인 시작
    compute_descriptor();
    init_pipelines();
    init_imgui();
    init_default_position();
    init_default_data();

    //buffer_set_up
    cullComputePipeline.culling_create_buffer(this, &chunkSystem);
    rayTracer.initRayTracingFunctions(_device);

    //엔진이 준비가 완료 됬으므로 true
    _isInitialized = true;
    _chunkIntialized = true;
    _initialize_once = true;
}
//frsutum culling(1.0~-1.0까지 화면에 전체 크기인데 이거 밖으로 나가면 안그린다)
bool is_visible(const RenderObject& obj, const glm::mat4& viewproj) {
    //일단은 꼭지점을 만들기
    std::array<glm::vec3, 8> corners {
        glm::vec3 { 1, 1, 1 },
        glm::vec3 { 1, 1, -1 },
        glm::vec3 { 1, -1, 1 },
        glm::vec3 { 1, -1, -1 },
        glm::vec3 { -1, 1, 1 },
        glm::vec3 { -1, 1, -1 },
        glm::vec3 { -1, -1, 1 },
        glm::vec3 { -1, -1, -1 },
    };

    //최종 공간 view*proj*model matrix
    glm::mat4 matrix = viewproj * obj.transform;

    //일단 1.0으로 맞춰도 되지만 vkguide에서 1.5로 맞추라고 했다
    glm::vec3 min = { 1.5, 1.5, 1.5 };
    glm::vec3 max = { -1.5, -1.5, -1.5 };

    for (int c = 0; c < 8; c++) {
	// 방금 만든 view*proj*model을 matrix라고 부르자
	//로컬 공간에서 꼭지점 찾기
        glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);

	//Homogeneous Coordinates(proj를 맞추기 위해서 0.0~1.0까지 맞추기 위해)어쩔수 없이 해야함
        v.x = v.x / v.w;
        v.y = v.y / v.w;
        v.z = v.z / v.w;

	//가장 작은 범위와 가장 큰 범위를 일단은 찾음
        min = glm::min(glm::vec3 { v.x, v.y, v.z }, min);
        max = glm::max(glm::vec3 { v.x, v.y, v.z }, max);
    }

    // 1.0밖으로 나간것은 안그린다 이말이다
    if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f || min.y > 1.f || max.y < -1.f) {
        return false;
    } else {
        return true;
    }
}
void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
    //밑에서 내려온 부모 노드 위치 * 현제 작업중인 노드 위치
    //예: 자전거 그자체 위치 * 바퀴 위치
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    //이거는 매인메쉬(MeshAsset)의 자식 subMesh(GeoSurface)를 뜻한다
    //vk_loader을 가서 확인을 해봐
    for (auto& s : mesh->surfaces) {
	RenderObject def;
	def.indexCount = s.count;
	def.firstIndex = s.startIndex;
	def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
	def.material = &s.material->data;
	def.bounds = s.bounds;
	//subMesh에다가 최종 데이터를 넣는거임
	def.transform = nodeMatrix;
	def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

	if (s.material->data.passType == MaterialPass::Transparent) {
            ctx.TransparentSurfaces.push_back(def);
        } else {
            ctx.OpaqueSurfaces.push_back(def);
        }
    }

    //제귀함수로 수많은 노드들을 다 반복을 하삼
    Node::Draw(topMatrix, ctx);
}
void VulkanEngine::draw_background(VkCommandBuffer cmd)
{
    //------------------컴퓨트 쉐이더 테스트-------------------------------------------
    //파이프라인 가지고 오기
    ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];

    //파이프라인 bind하기
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    //실제 디스크립터 가지고와서 연결
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

    //ComputePushConstants를 너주는 거임 근데 이제는 pipeline안에 data를 넣는거임
    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

    /*푸쉬 컨스턴트 등록
    ComputePushConstants pc;
    pc.data1 = glm::vec4(1, 0, 0, 1);
    pc.data2 = glm::vec4(0, 0, 1, 1);
    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pc);*/

    // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it(실행)
    vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);

    //----------------------------------------------------------------------------------

    //화면 출력 태스트------------------------------------------------------------------

    /*그냥 화면이 잘 출력이 되는지 일종에 태스트??(화면에서 파란색이 나와야함)
    VkClearColorValue clearValue;
    float flash = std::abs(std::sin(_frameNumber / 120.f));
    clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

    //이제 도화지를 다시 지울건데 depth말고 color만
    VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

    //clear image
    vkCmdClearColorImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);*/
    //----------------------------------------------------------------------------------
}
void VulkanEngine::init_default_position(){
    //시작점이다 카메라에 너가 원하는 위치에 마음대로 세팅을 하삼
    mainCamera.velocity = glm::vec3(0.f);
    //놀랍게도 -z가 앞이다 그래서 우리는 물채를 5만큼 밀어낸다고 생각을 하면 편하다
    mainCamera.position = glm::vec3(0, 5, 5);
    mainCamera.pitch = 0;
    mainCamera.yaw = 0;
}
void VulkanEngine::init_default_data() {
    //이거는 그냥 샊갈 color
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    //사이즈는 1*1 사이즈 픽샐을 만들거임
    _whiteImage = create_image((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    _greyImage = create_image((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
    _blackImage = create_image((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    //디테일하게 알 필요는 없고 그냥 checkerboard임 약간 마크에서 꺠진 택스쳐라고 생각을 하면 됌.
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 *16 > pixels; //for 16x16 checkerboard texture
    for (int x = 0; x < 16; x++) {
	for (int y = 0; y < 16; y++) {
	    pixels[y*16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
	}
    }
    //그리고 현제 16*16 256개 있는 픽샐 데이터들을 그래도 이미지로 생성을 해.
    _errorCheckerboardImage = create_image(pixels.data(), VkExtent3D{16, 16, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    //어떻게 이미지들을 읽을 것인지 dictate하는것이다 --> 늘릴건지, 이미지가 반복할것인지 etc
    VkSamplerCreateInfo sampl = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    //우리는 16*16 픽샐은 너무 작으니 일단은 늘리는 선택을 한다(날카롭게 edges no blur)
    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;

    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest);

    //늘리지만 쵀대한 부드럽게 경계선을
    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear);

    GLTFMetallic_Roughness::MaterialResources materialResources;
    //그냥 우리가 쓰던 white색상을 우리 MaterialResources에다가 넣는다
    materialResources.colorImage = _whiteImage;
    materialResources.colorSampler = _defaultSamplerLinear;
    materialResources.metalRoughImage = _whiteImage;
    materialResources.metalRoughSampler = _defaultSamplerLinear;

    //이제 모든걸 담을 버퍼를 memeory allocate를 해줘야함
    AllocatedBuffer materialConstants = create_buffer(sizeof(GLTFMetallic_Roughness::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    //버퍼 정보 주입 (여기서 바로 memcpy를 할필요 없이 CPU to GPU 포인터를 가지고 옴)
    //포인터를 통해서 CPU to GPU가 서로 데이터를 주고받을 수 있음
    GLTFMetallic_Roughness::MaterialConstants* sceneUniformData = (GLTFMetallic_Roughness::MaterialConstants*)materialConstants.allocation->GetMappedData();
    sceneUniformData->colorFactors = glm::vec4{1,1,1,1};
    sceneUniformData->metal_rough_factors = glm::vec4{1,0.5,0,0};

    materialResources.dataBuffer = materialConstants.buffer;
    materialResources.dataBufferOffset = 0;

    defaultData = metalRoughMaterial.write_material(_device,MaterialPass::MainColor,materialResources, globalDescriptorAllocator);

    std::string structurePath = { ".\\assets\\basicmesh.glb" };
    auto structureFile = loadGltf(this,structurePath);
    assert(structureFile.has_value());
    loadedScenes["Object_0"] = *structureFile;

    _mainDeletionQueue.push_function([=, this]() {
	destroy_buffer(materialConstants);
    });
    //delete the rectangle data on engine shutdown
    _mainDeletionQueue.push_function([&](){
	destroy_buffer(_gpuMeshBuffer.indexBuffer);
	destroy_buffer(_gpuMeshBuffer.vertexBuffer);

	vkDestroySampler(_device,_defaultSamplerNearest,nullptr);
	vkDestroySampler(_device,_defaultSamplerLinear,nullptr);

	destroy_image(_whiteImage);
	destroy_image(_greyImage);
	destroy_image(_blackImage);
	destroy_image(_errorCheckerboardImage);
    });
}
void VulkanEngine::update_scene()
{
    mainCamera.update();
    glm::mat4 view = mainCamera.getViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)_windowExtent.width / (float)_windowExtent.height, 10000.f, 0.1f);
    projection[1][1] *= -1;

    mainDrawContext.OpaqueSurfaces.clear();
    mainDrawContext.TransparentSurfaces.clear();

    //Suzanne이라는 이름을 가진 Node를 Draw()를 해서 다 나눔
    //mainDrawContext는 최종적으로 RenderObject라는 struct를 담고 있어
    loadedScenes["Object_0"]->Draw(glm::mat4{ 1.f }, mainDrawContext);

    //(model)
    //사각형 하나 데이터 Modle Matrix 설정을 하는것이다
    //voxelModel.singleTileModel = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    voxelModel.singleTileModel = glm::mat4(1.f);

    //{fragmentShaderData}넣기
    fragmentData.screenSize = glm::vec4(
	(float)_windowExtent.width,
	(float)_windowExtent.height,
	1.0f / (float)_windowExtent.width,
	1.0f / (float)_windowExtent.height
    );

    //카메라 관련(view , projection)
    sceneData.view = view;
    sceneData.proj = projection;
    sceneData.viewproj = projection * view;

    //샊갈 관련 된 데이터들
    sceneData.ambientColor = glm::vec4(.1f);
    sceneData.sunlightColor = glm::vec4(1.f);
    sceneData.sunlightDirection = glm::vec4(0,1,0.5,1.f);

    //rayTraicng 관련 uniform buffer data 삽입
    SceneDataBuffer scene;
    scene.view = view;
    scene.proj = projection;
    scene.viewProj = projection * view;
    scene.cameraPosition = glm::vec4(mainCamera.position, 1.0);
    scene.sunColor = glm::vec4(1.f);
    scene.sunDirection = glm::vec4(0, 1, 0.5f, 1.f);
    rayTracingUniformBuffer.sceneData = scene;
}

void VulkanEngine::compute_buffer(){
    if (chunkSystem._meshBuffers.vertexBuffer.buffer == VK_NULL_HANDLE) {
        current_vertcies_size = chunkSystem.indirect_buffer_size * chunkSystem.max_buffer;
	maxIndicies = current_indices_size;
	chunkSystem._meshBuffers.vertexBuffer = create_buffer(
	    sizeof(Vertex) * current_vertcies_size, 
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
	    VMA_MEMORY_USAGE_GPU_ONLY
	);
	chunkSystem._meshBuffers.chunkMetaBuffer = create_buffer(
	    sizeof(ChunkMetaData) * chunkSystem.indirect_buffer_size,
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
	    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
	    VMA_MEMORY_USAGE_CPU_TO_GPU
	);
	chunkSystem._meshBuffers.indirectRangeBuffer = create_buffer(
	    sizeof(VkAccelerationStructureBuildRangeInfoKHR) * chunkSystem.indirect_buffer_size,
	    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
	    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | 
	    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
	    VMA_MEMORY_USAGE_CPU_TO_GPU
	);

	// Deletion Queue에는 프로그램 종료 시 파기할 용도로 딱 1번만 등록!
	AllocatedBuffer tempBuffer1 = chunkSystem._meshBuffers.chunkMetaBuffer;
	AllocatedBuffer tempBuffer2 = chunkSystem._meshBuffers.vertexBuffer;
	AllocatedBuffer tempBuffer3 = chunkSystem._meshBuffers.indirectRangeBuffer;
	_mainDeletionQueue.push_function([this, tempBuffer1, tempBuffer2, tempBuffer3](){
	    destroy_buffer(tempBuffer3);
	    destroy_buffer(tempBuffer2);
	    destroy_buffer(tempBuffer1);
	});
	compute_address_helper(&chunkSystem._meshBuffers); 
    }

}
void VulkanEngine::compute_address_helper(GPUMeshBuffers* meshBuffer){
    VkBufferDeviceAddressInfo addrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };

    addrInfo.buffer = meshBuffer->vertexBuffer.buffer;
    meshBuffer->vertexBufferAddress = vkGetBufferDeviceAddress(_device, &addrInfo);

    addrInfo.buffer = meshBuffer->chunkMetaBuffer.buffer;
    meshBuffer->chunkMetaBufferAddress = vkGetBufferDeviceAddress(_device, &addrInfo);

    addrInfo.buffer = meshBuffer->indirectRangeBuffer.buffer;
    meshBuffer->indirectRangeBufferAddress = vkGetBufferDeviceAddress(_device, &addrInfo);
}

void VulkanEngine::draw_geomatry(VkCommandBuffer cmd){
    //이제 방금전까지 무슨 파이프라인, MaterialInstance, 그리고 indexbuffer을 썻는지 저장을 하는곳
    MaterialPipeline* lastPipeline = nullptr;
    MaterialInstance* lastMaterial = nullptr;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    //디버깅 관련 변수 호출하는거임
    stats.drawcall_count = 0;
    stats.triangle_count = 0;

    auto start = std::chrono::system_clock::now();

    std::vector<uint32_t> opaque_draws;
    //OpaqueSurfaces만큼 공간을 일당 할당(노드(부품) 하나하나를 OpaqueSurfaces라고 부른다(불투명 기준)
    opaque_draws.reserve(mainDrawContext.OpaqueSurfaces.size());

    for (uint32_t i = 0; i < mainDrawContext.OpaqueSurfaces.size(); i++) {
	//만약 범위 안이다??(frustum culling)
	if (is_visible(mainDrawContext.OpaqueSurfaces[i], sceneData.viewproj)) {
	    //일단 화면에 있는것 만큼 공간에 숫자들을 하나씩 넣는다
	    opaque_draws.push_back(i);
	}
    }

    //인덱스 배열(opaque_draws)을 '머티리얼과 인덱스 버퍼 주소' 기준으로 정렬한다!
    std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto& iA, const auto& iB) {
	const RenderObject& A = mainDrawContext.OpaqueSurfaces[iA];
	const RenderObject& B = mainDrawContext.OpaqueSurfaces[iB];
	if (A.material == B.material) {
	    // 1단계: 두 물체의 (머티리얼)가 같다면?
	    // indexBuffer 넘어가서 (인덱스 버퍼 주소) 크기 순으로 정렬해라!
	    return A.indexBuffer < B.indexBuffer;
	}
	else {
	    // 1단계: 두 물체의 머티리얼가 서로 다르다면?
	    // 그냥 mateiral을 크기 순으로 나열을 해라
	    return A.material < B.material;
	}
	//어찌보면 같은 material끼리 쵀대한 묶는거임
	//이레야 마테리얼이 계속 바뀌지 않음
    });

    //attachemntsinfo--> colorAttachement일단은 생성
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depthAttchment = vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    //----------------------------------create uniform buffer--------------------------------------------------------

    //유니폼 버퍼 생성 및 allocate(vma 한테 CPU에서 쓰고 GPU가 읽는 형식으로 할거임
    AllocatedBuffer gpuSceneDataBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    //유니폼 생성 그리고 allocate --> 근데 이거는 우리 voxel 관련 데이터임
    AllocatedBuffer voxelModelDataBuffer = create_buffer(sizeof(voxelModel), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    //fragment shader전용 UBO를 만드는 곳
    AllocatedBuffer fragmentDataBuffer = create_buffer(sizeof(FragmentUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    rayTracingUniformBuffer.RayTracerShaderBuffer = create_buffer(sizeof(SceneDataBuffer), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    //메 프레임마다 쓰고 버릴거니깐 이런 코드를 쓰삼
    AllocatedBuffer tempBuffer = rayTracingUniformBuffer.RayTracerShaderBuffer;
    get_current_frame()._deletionQueue.push_function([=, this]() {
	destroy_buffer(gpuSceneDataBuffer);
	destroy_buffer(voxelModelDataBuffer);
	destroy_buffer(fragmentDataBuffer);
	destroy_buffer(tempBuffer);
    });

    //-----------------------------------------------------------------------------------------------------------------

    //-------------------------------putting the data at our uniform buffer----------------------------------------------
    
    DescriptorWriter writer;

    //GPU안에 생존을 하고 있는 진짜 메모리 주소를 가지고 오삼 (VkBuffer real memeory)
    GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
    //그리고 복사를 하는것이다 즉 집어넣는거임(CPU memeory --> GPU Vram)
    //host visible 즉 우리가 앞서 VMA한테 VMA_MEMORY_USAGE_CPU_TO_GPU라고 언급을 해서 이게 가능함
    //memcpy로 복사를 할 필요가 없음(그것은 오로지 staging buffer만)
    *sceneUniformData = sceneData;

    //이거는 vk_descriptor 에 있는 allocate함수이고 여기서 VRAM에다가 set메모리를 할당하고 그 set을 이제 넘겨줌
    VkDescriptorSet globalDescriptor = get_current_frame()._frameDescriptors.allocate(_device, _gpuSceneDataDescriptorLayout);

    //update및 write 이 2개가 뭐하는지에 대한 정보는 vk_descriptor더 디테일하게 있으니 찾아가서 보삼
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_device, globalDescriptor);
    writer.clear();
    //UBO는 현제 안쓰니깐 바인드는 생략을 한다 만약 필요하면 날 풀어줘
    //vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicsPipelineLayout, 0, 1, &globalDescriptor, 0, nullptr);
    
    VoxelModel* voxelModelUniformData = (VoxelModel*)voxelModelDataBuffer.allocation->GetMappedData();
    *voxelModelUniformData = voxelModel;
    VkDescriptorSet voxelDescriptor = get_current_frame()._frameDescriptors.allocate(_device, _voxelModelDescriptorLayout);
    //binding 은 3에다가 하기
    writer.write_buffer(3, voxelModelDataBuffer.buffer, sizeof(VoxelModel), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_device, voxelDescriptor);
    writer.clear();
    //set는 1번으로 지정(이유?? 공간이 꽉참 ㅠㅠ)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicsPipelineLayout, 1, 1, &voxelDescriptor, 0, nullptr);

    FragmentUniform* fragmentUniformData = (FragmentUniform*)fragmentDataBuffer.allocation->GetMappedData();
    *fragmentUniformData = fragmentData;
    VkDescriptorSet fragmentDescriptor = get_current_frame()._frameDescriptors.allocate(_device, _fragmentShaderDescriptorLayout);
    //binding은 0에다가 하겠다
    writer.write_buffer(0, fragmentDataBuffer.buffer, sizeof(FragmentUniform), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_device, fragmentDescriptor);
    writer.clear();
    //set2번 에다가 넣었다.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicsPipelineLayout, 2, 1, &fragmentDescriptor, 0, nullptr);

    SceneDataBuffer* sceneUniformBuffer = (SceneDataBuffer*)rayTracingUniformBuffer.RayTracerShaderBuffer.allocation->GetMappedData();
    *sceneUniformBuffer = rayTracingUniformBuffer.sceneData;
    VkDescriptorSet sceneDescriptor = get_current_frame()._frameDescriptors.allocate(_device, rayTracingUniformBuffer.RayTracerDescriptorLayout);
    //binding은 0에다가 하겠다
    writer.write_buffer(0, rayTracingUniformBuffer.RayTracerShaderBuffer.buffer, sizeof(SceneDataBuffer), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_device, sceneDescriptor);
    writer.clear();
    //set3번 에다가 넣었다.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicsPipelineLayout, 3, 1, &sceneDescriptor, 0, nullptr);

    //-----------------------------------------------------------------------------------------------------------------
    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttchment);

    //랜더링 시작(다이네믹 랜더링aka랜더패스는 자동으로 시작을 한다)
    vkCmdBeginRendering(cmd, &renderInfo);

    //파이프라인 커멘드 버퍼와 연결 (파이프라인하고)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicsPipeline);
    //각각 프레임 마다 우리 메쉬(색갈 프라그먼트 쉐이더) 관련 디스크립터
    VkDescriptorSet imageSet = get_current_frame()._frameDescriptors.allocate(_device, _singleImageDescriptorLayout);
    {
	DescriptorWriter writer;
	writer.write_image(0, _errorCheckerboardImage.imageView, _defaultSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	writer.update_set(_device, imageSet);
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicsPipelineLayout, 0, 1, &imageSet, 0, nullptr);

    rayTracer.bindDescriptorSet(this, &cmd, &rayTracingBuffer);

    PipelineSetter setter;

    VkViewport viewport = setter.viewport(_windowExtent.width, _windowExtent.height);
    VkRect2D scissor = setter.scissor(_windowExtent.width, _windowExtent.height);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    //사각형 그리기
    DrawMeshBuffer push_constants;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicsPipeline);
    push_constants.worldMatrix = sceneData.proj * sceneData.view;
    push_constants.outputVertexBuffer = chunkSystem._meshBuffers.vertexBufferAddress;
    vkCmdPushConstants(cmd, _graphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DrawMeshBuffer), &push_constants);
    vkCmdDrawIndirectCount(cmd, chunkSystem._meshBuffers.finalIndirectBuffer.buffer, 0, chunkSystem._meshBuffers.drawCounterBuffer.buffer, 0, chunkSystem.indirect_buffer_size, sizeof(VkDrawIndirectCommand));

    auto draw = [&](const RenderObject& r) {
	//material이 직전에 그린것하고 다르다??
	if (r.material != lastMaterial) {
	    //파이프라인 업데이트 이제 새로운 파이프라인으로 렌더링 할것이다
	    lastMaterial = r.material;
	    //이제 파이프라인이 바뀌었으니 다시 rebiding
	    if (r.material->pipeline != lastPipeline) {
		lastPipeline = r.material->pipeline;
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,r.material->pipeline->layout, 0, 1, &globalDescriptor, 0, nullptr);
	    }
	    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 1, 1, &r.material->materialSet, 0, nullptr);
	}
	//설마 indexBuffer까지 바뀌었다면!!!
	if (r.indexBuffer != lastIndexBuffer) {
	    //이것까지 업데이트
	    lastIndexBuffer = r.indexBuffer;
	    vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
	}
	// 이제 랜더링을 해도 된다
	// 만약 파이프라인 중간에 바뀌면 위에 forloop긜고 if문이 다 알아서 해준다
	DrawMeshBuffer push_constants;
	push_constants.worldMatrix = r.transform;
	push_constants.outputVertexBuffer = r.vertexBufferAddress;
	vkCmdPushConstants(cmd, r.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DrawMeshBuffer), &push_constants);
	//vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
	//stats
	stats.drawcall_count++;
	stats.triangle_count += r.indexCount / 3;
    };
    for (auto& r : opaque_draws) {
	draw(mainDrawContext.OpaqueSurfaces[r]);
    }

    for (auto& r : mainDrawContext.TransparentSurfaces) {
	draw(r);
    }

    vkCmdEndRendering(cmd);

    auto end = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats.mesh_draw_time = elapsed.count() / 1000.f;
}
void VulkanEngine::draw()
{
    //이전 프레임 랜더링을 기다려라 CPU는 (GPU가 끝나면 시작)
    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
    update_scene();
    //일단 지울건 모두다 지우고 본다
    get_current_frame()._deletionQueue.flush();
    //디스크립터 풀을 한번 비우고 랜더링을 시작하자
    get_current_frame()._frameDescriptors.clear_pools(_device);

    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
    //일단은 윈도우 사이즈부터 정함
    //내가 마지노선을 넘지 않도록 정해주는 거임(참고로 여기서는 우리가 window resize를 할떄 spwapchain을 바꿔줌)
    _drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * renderScale;
    _drawExtent.width= std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * renderScale;
    //스왑체인 안에서 빈 도화지를 가지고 온다
    uint32_t swapchainImageIndex;
    //만약 swapchain 안에 빈 도화지가 없다면 1초를 기다린다
    //여기서 swapchain semaphore를 켜준다
    VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex);
    //만약 이미지 사이즈가 바뀐다 (중간에 윈도우를 조금더 작게 마우스로 드래그 한다고 치면)
    if (e == VK_ERROR_OUT_OF_DATE_KHR) {
	_resize_requested = true;       
	//리턴을 하는이유는 일단 에러가 났으니 빠져노와서 image가 resize된 후 다시 aquire한다는 의미
	return ;
    }

    //일단 command buffer들을 복사를해서 온다
    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

    //이제 command buffer들의 execution(이전 프레임)이 끝났으니 safely reset
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    //다시 우리가 렌더링에 필요한 커멘드를 기록한다
    //VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT --> 그냥 딱 한번 전달 및 실행이 된다(필요는 없는데 약간 빨라짐)
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));	
    //VBO데이터들 설정을 하기
    chunkSystem.frame_chunk_generation(this, &voxelPipeline ,cmd, &chunkSystem._meshBuffers);

    //UBO셋팅 및 값 넣기
    cullComputePipeline.cull_compute_descriptor(this, &cullUniformBuffer, sceneData, &chunkSystem, &_windowExtent);
    executeCulling.execute_culling(this, &cullUniformBuffer, &chunkSystem, &cullCompute, cmd, &_cullPipelineLayout, &chunkSystem._meshBuffers);

    //현제는 transition (undeifned --> layout genral(우리가 쉐이더로 image에다가 원하는걸 쓸 수 있게)
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    
    rayTracer.activateRayTacing(this, &chunkSystem, &cmd, &chunkSystem._meshBuffers, &rayTracingBuffer);
    //배경을 그려넣는다
    draw_background(cmd);

    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    //draw_geomatry 를 그리기
    draw_geomatry(cmd);
    //이제 다 그렸으니 전송 상태에서 대기(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    //이거는 받는 것이다 다 끝난 이미지는 여기 스왑체인에다가 넣을 것이다 그러므로 (VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)로 두는것이다
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // 우리는 _drawImage.image(다 그린)를 스왑체인에다가 복사를 할것이다
    vkutil::copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

    // 스왑체인 안에 있는 다 그린 그림을 다시 바꿈 colorAttachment로(또 그려야함)
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    //이제 UI (IMGUI)를 추가로 그릴거임
    draw_imgui(cmd, _swapchainImageViews[swapchainImageIndex]);

    // 이제 layout을 present mode로 바꾸면 끝이다
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    //finalize the command buffer (we can no longer add commands, but it can now be executed)
    VK_CHECK(vkEndCommandBuffer(cmd));

    //그냥 커멘드 버퍼
    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);	
    //swapchian semaphore의 info를 부여하는 방법
    //VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR --> 의미는 도화지에 샊갈을 부여하는 전 단계까지만 기다리삼
    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,get_current_frame()._swapchainSemaphore);
    //render semaphore aka present semaphore이다
    //VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT --> 이 뜻은 그림을 다 그렸다. 만약 이 단계까지 왔다면 GPU는 다음 단계로 이동을 해도 된다.
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, _renderSemaphores[swapchainImageIndex]);	
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo,&signalInfo,&waitInfo);	
    //그래픽스 큐에다가 semaphore그리고 커멘드 버퍼를 전달한다
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

    //이제 grpahics queue 본인 할일을 다 한 이후 짤막한 내용이다
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &_swapchain;
    presentInfo.swapchainCount = 1;
    //GPU가 다 그리기 전까지 기다리는 Semaphore(render Semaphore)이다
    presentInfo.pWaitSemaphores = &_renderSemaphores[swapchainImageIndex];
    //여기서 redner semaphore만 기다리면 되는 이유로 1개다
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &swapchainImageIndex;

    //이건 실제로 보여주는 코드이다
    VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
	_resize_requested = true;
    }
    //increase the number of frames drawn(말 그대로다)
    _frameNumber++;
}

void VulkanEngine::run()
{
    SDL_Event e;
    //이게 true면 프로그램은 꺼지지않고 계속 진행
    bool bQuit = false;

    // main loop
    while (!bQuit) {
	auto start = std::chrono::system_clock::now();
	SDL_SetWindowRelativeMouseMode(_window, mainCamera.activation);
	if(mainCamera.activation){
	    SDL_WarpMouseInWindow(_window, _windowExtent.width/2.0, _windowExtent.height/2.0);
	}
        auto currentTime = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
	lastTime = currentTime;
	mainCamera.deltaTime = deltaTime;
	if (++frameCount >= 100) {
	    float elapsed = std::chrono::duration<float>(currentTime - lastFpsTime).count();
	    if (elapsed > 0.0f) {
		float avgFps = 100.0f / elapsed;
		float avgMs = (elapsed / 100.0f) * 1000.0f;
		std::cout << "FPS: " << avgFps << " (" << avgMs << " ms)\n";
	    }
	    frameCount = 0;
	    lastFpsTime = currentTime;
	}

        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_EVENT_QUIT){
                bQuit = true;
	    }
	    //이것은 그냥 유저가 창을 너무 줄이면 랜더링을 그만하고 다시 정상적으로 복구를 하면 다시 rendering을 키는 방식이야
	    if (e.type == SDL_EVENT_WINDOW_MINIMIZED) {
		stop_rendering = true;
	    }
	    if (e.type == SDL_EVENT_WINDOW_RESTORED) {
		stop_rendering = false;
	    }
	    mainCamera.ESCKey(e);
	    if(mainCamera.activation){
		mainCamera.processSDLEvent(e);
	    }
	    ImGui_ImplSDL3_ProcessEvent(&e);
        }
        if (stop_rendering) {
            // cpu를 100ms만큼 잠재워주는 역할
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
	if (_resize_requested) {
	    resize_swapchain();
	}

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	//디버깅 관련 코드
	ImGui::Begin("Stats");
        ImGui::Text("frametime %f ms", stats.frametime);
        ImGui::Text("draw time %f ms", stats.mesh_draw_time);
        ImGui::Text("update time %f ms", stats.scene_update_time);
        ImGui::Text("triangles %i", stats.triangle_count);
        ImGui::Text("draws %i", stats.drawcall_count);
        ImGui::End();

	if (ImGui::Begin("background")) {

	    ComputeEffect& selected = backgroundEffects[currentBackgroundEffect];
	    ImGui::Text("Selected effect: %s", selected.name);
	    ImGui::SliderInt("Effect Index", &currentBackgroundEffect,0, backgroundEffects.size() - 1);

	    ImGui::InputFloat4("data1",(float*)& selected.data.data1);
	    ImGui::InputFloat4("data2",(float*)& selected.data.data2);
	    ImGui::InputFloat4("data3",(float*)& selected.data.data3);
	    ImGui::InputFloat4("data4",(float*)& selected.data.data4);

	    ImGui::SliderFloat("Render Scale",&renderScale, 0.3f, 1.f);
	}
	ImGui::End();
	//make imgui calculate internal draw structures
	ImGui::Render();
        draw();

	//일 다 끝났으니까 끝난 시점의 시계를 다시 찍는다! (스톱워치 스톱)
	auto end = std::chrono::system_clock::now();
	// [종료 시간 - 시작 시간]을 해서 걸린 시간을 마이크로초(1/1,000,000초) 단위 정수로 변환한다.
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	// 마이크로초를 1000.0f로 나눠서 밀리초(ms, 1/1,000초) 소수점으로 바꾸고 stats.frametime에 저장한다.
	stats.frametime = elapsed.count() / 1000.f;
    }
}
void VulkanEngine::cleanup()
{
    if (_isInitialized) {
	//일단 GPU가 다 끝날떄가지 기다리삼
	vkDeviceWaitIdle(_device);
	loadedScenes.clear();


	for (int i = 0; i < FRAME_OVERLAP; i++) {
	    vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
	    vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
	    vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);
	    _frames[i]._deletionQueue.flush();
	}
	for (int i = 0; i < swapchainImageCount; i++){
	    vkDestroySemaphore(_device ,_renderSemaphores[i], nullptr);
	}
	for (auto& mesh : testMeshes) {
	    destroy_buffer(mesh->meshBuffers.indexBuffer);
	    destroy_buffer(mesh->meshBuffers.vertexBuffer);
	}

	_mainDeletionQueue.flush();

	destroy_swapchain();

	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyDevice(_device, nullptr);

	vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	//instance를 가장 나중에 해지를 해야함
	vkDestroyInstance(_instance, nullptr);
	SDL_DestroyWindow(_window);
    }
}
