#pragma once

#include "culling.h"
#include "glm/ext/vector_float4.hpp"
#include "vk_types.h"
#include "vk_descriptors.h"
#include "vk_loader.h"
#include "vk_scene.h"
#include "camera.h"
#include "voxel_compute_pipeline.h"
#include "voxeldata.h"
#include "mac_algoo_data.h"
#include "raytracing.h"

#include "cull_pipeline.h"

#include <cstdint>
#include <span>

struct DeletionQueue
{
    //모든 delete과련된 함수를 담는 것이다
    std::deque<std::function<void()>> deletors;
    void push_function(std::function<void()>&& function) {
	//걍 한곳에 담는 함수
	deletors.push_back(function);
    }

    //flush즉 뒤에서부터 앞에까지 지운다 차례대로
    void flush() {
	for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
	    (*it)(); //call functors
	}

	deletors.clear();
    }
};
struct MeshNode : public Node {
    std::shared_ptr<MeshAsset> mesh;
    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
};
struct RenderObject {
    uint32_t indexCount;
    uint32_t firstIndex;
    VkBuffer indexBuffer;

    MaterialInstance* material;
    //vk_loader.h에 보면 모든 데이터가 있음
    Bounds bounds;

    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;
};
struct DrawContext {
    std::vector<RenderObject> OpaqueSurfaces;
    std::vector<RenderObject> TransparentSurfaces;
};

struct GLTFMetallic_Roughness {
    //파이프라인을 2개로 나누는거임
    //하나는 그냥 평범한 물체
    MaterialPipeline opaquePipeline;
    //하나는 그냥 반투명 혹은 투명한 물체 파이프라인들
    MaterialPipeline transparentPipeline;

    //여기서 사용될 버퍼들 연결
    //재질은 버퍼 1개(MaterialConstants)
    //텍스처 버퍼 2개(Color 텍스처, Metal-Rough 텍스처)
    VkDescriptorSetLayout materialLayout;

    //메쉬에 색깔 및 금속 데이터들
    struct MaterialConstants {
	//색깔 데이터들 RGBA
	glm::vec4 colorFactors;
	//이것은 걍 금속 간련된 데이터들
	glm::vec4 metal_rough_factors;
	//padding, we need it anyway for uniform buffers
	glm::vec4 extra[14];
    };

    struct MaterialResources {
	AllocatedImage colorImage;
	VkSampler colorSampler;
	AllocatedImage metalRoughImage;
	VkSampler metalRoughSampler;
	VkBuffer dataBuffer;
	uint32_t dataBufferOffset;
    };

    DescriptorWriter writer;

    void build_pipelines(VulkanEngine* engine);
    void clear_resources(VkDevice device);

    MaterialInstance write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator);
};

//command buffer 관련된 구조체
struct FrameData {
    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;

    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    VkFence _renderFence;

    DeletionQueue _deletionQueue;
    DescriptorAllocatorGrowable _frameDescriptors;
};

//PushConstants 관련 구조체
struct ComputePushConstants {
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};
//파이프라인
struct ComputeEffect {
    const char* name;
    VkPipeline pipeline;
    VkPipelineLayout layout;

    ComputePushConstants data;
};

//디버깅 관련 코드들 얼마나 빠릴 움직이는지 알 수 있다.
struct EngineStats {
    float frametime;
    int triangle_count;
    int drawcall_count;
    float scene_update_time;
    float mesh_draw_time;
};

//더블 버퍼링 즉 하나는 gpu에서 executing 하는 동안에 하나는 vkcmdxxxx로 record 당하는 거임(동시에 두개의 작업을 하는거다)
constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:
    float deltaTime;
    //engine이 재대로 켜졌는지
    bool _isInitialized{ false };
    bool _initialize_once { false };
    bool _chunkIntialized{ false };

    //몇번째 프레임을 그려주고 있어??
    int _frameNumber {0};
    bool stop_rendering{ false };
    bool _resize_requested;
    VkExtent2D _windowExtent{ 1700 , 900 };
    //struct를 붙침으로써 일단은 구조체만 만들고 내가 디테일은 cpp파일에다가 더 디테일하게 설명할게
    //SDL_Window* 주소값만 저장할 자리만 만들어 일단은
    struct SDL_Window* _window{ nullptr };
    //디자인 패턴:VulkanEngine::Get()를 쓰는거 만으로도 어디에서든 엔진 내부를 쓸수 있게금 설정

    //디버깅 툴
    EngineStats stats;
    //카메라 관련
    Camera mainCamera;
    //ReyTracing 관련된 툴
    RayTracingTool rayTracer;

    static VulkanEngine& Get();

    //전체적인 setup과정
    VkInstance _instance;// Vulkan library handle
    VkDebugUtilsMessengerEXT _debug_messenger;// Vulkan debug output handle
    VkPhysicalDevice _chosenGPU;// GPU chosen as the default device
    VkDevice _device; // Vulkan Logical device for commands
    VkSurfaceKHR _surface;// Vulkan window surface
    
    //텍스쳐 관리
    AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    void destroy_image(const AllocatedImage& img);

    //이미지 관리 temporary 나중에 바뀔수 있음.
    AllocatedImage _whiteImage;
    AllocatedImage _blackImage;
    AllocatedImage _greyImage;
    AllocatedImage _errorCheckerboardImage;
    VkSampler _defaultSamplerLinear;
    VkSampler _defaultSamplerNearest;

    //마테리얼 관련된 코드들
    MaterialInstance defaultData;
    GLTFMetallic_Roughness metalRoughMaterial;
    
    //씬 관련 데이터
    GPUSceneData sceneData;

    VoxelModel voxelModel;
    ChunkSystem chunkSystem;
    VoxelPipeline voxelPipeline;

    //Culling 관련된 버퍼 데이터
    ExecuteCulling executeCulling;

    //framgemnt UBO를 보낼 데이터들
    FragmentUniform fragmentData;

    //컴퓨트 쉐이더 파이프라인
    VkPipelineLayout _gradientPipelineLayout;
    VkPipelineLayout _voxeldataPipelineLayout;
    VkPipelineLayout _cullPipelineLayout;
    CullComputePipelineData cullCompute;

    VoxelComputePipeline _voxelCountPipeline;
    VoxelComputePipeline _prefixPipeline;

    //컴퓨트파이프라인 셋업
    CullComputePipeline cullComputePipeline;

    //그래픽스 파이프라인
    VkPipelineLayout _graphicsPipelineLayout;
    VkPipeline _graphicsPipeline;

    GPUMeshBuffers _outputMeshBuffer;
    GPUMeshBuffers _gpuMeshBuffer;

    //swapchain setup과정
    VkSwapchainKHR _swapchain;
    //Image data type을 읽는 방법:이 찰흙은 8비트짜리 빨/초/파/투명도로 이루어진 찰흙이야
    VkFormat _swapchainImageFormat;

    std::vector<VkImage> _swapchainImages;
    //이 찰흙 덩어리의 2번째 층(Mipmap)만, 흑백 렌즈(VkFormat)를 끼울거야--> 이미지의 전체적인 설명도
    std::vector<VkImageView> _swapchainImageViews;
    VkExtent2D _swapchainExtent;

    //command buffer 관련된 코드
    FrameData _frames[FRAME_OVERLAP];
    FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };

    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;

    std::vector<VkSemaphore> _renderSemaphores;
    uint32_t swapchainImageCount;

    //우리 씬 관련 필드들
    DrawContext mainDrawContext;
    std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes;

    //우리의 각각 scene들을 여기에다가 하나하나 저장을 함
     std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;
    void update_scene();

    //DeletionQueue
    DeletionQueue _mainDeletionQueue;

    //VMA
    VmaAllocator _allocator;
    AllocatedImage _drawImage;
    AllocatedImage _depthImage;
    //decide the size of yo image
    VkExtent2D _drawExtent;
    float renderScale = 1.f;

    //디스크립터
    //새로운 CullUniformBufferData
    CullUniformBufferData cullUniformBuffer;
    NoiseUniformBuffer noiseUniformBuffer;
    //새로운 디스크립터(래이트레이싱)
    RayTracerUniformBuffer rayTracingUniformBuffer;
    RayTracingBuffer rayTracingBuffer;

    DescriptorAllocatorGrowable globalDescriptorAllocator;
    VkDescriptorSet _drawImageDescriptors;
    VkDescriptorSet _marchingCubeDescriptor;

    VkDescriptorSetLayout _drawImageDescriptorLayout;
    VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;
    VkDescriptorSetLayout _singleImageDescriptorLayout;
    VkDescriptorSetLayout _voxelModelDescriptorLayout;
    VkDescriptorSetLayout _marchingCubeDescriptorLayout;
    VkDescriptorSetLayout _fragmentShaderDescriptorLayout;

    //immdeiate submit (imgui관련된 필드)
    VkFence _immFence;
    VkCommandBuffer _immCommandBuffer;
    VkCommandPool _immCommandPool;

    //Compute Pipeline 
    //파이프라인을 바꿔주는 백터
    std::vector<ComputeEffect> backgroundEffects;
    int currentBackgroundEffect{0};
    //그냥 컴퓨트 파이프라인
    ComputeEffect voxelComputeEffect;

    //메쉬 가지고 오기
    std::vector<std::shared_ptr<MeshAsset>> testMeshes;

    //vertex관리
    uint32_t indicies_size;

    //스왑체인 관련된 것들 만들기
    void create_swapchain(uint32_t width, uint32_t height);
    void init_swapchain();
    void destroy_swapchain();
    void resize_swapchain();

    //chunk 관리
    MarchingCubeData marchingCubeData;
    glm::vec4 starting_position;

    //파이프라인
    void init_pipelines();
    void init_background_pipelines();
    void init_compute_pipeline();
    void init_graphics_pipeline();

    //버퍼 생성 및 파괴for out VetexBuffer
    AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    uint64_t current_vertcies_size;
    uint64_t current_indices_size;
    uint64_t maxIndicies;
    void destroy_buffer(const AllocatedBuffer& buffer);

    //매프레임마다 버리지 않는 UBO
    AllocatedBuffer _marchingCubeBuffer;

    //필요한 VBO 그리고 IBO의 대한 정보를 담고 있어...
    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertcies);

    //전체적인 시작
    void init_vulkan();
    void init_commands();
    void init_sync_structures();
    void init_descriptors();
    void init_default_position();
    void init_default_data();
    //그냥 computeShader 버퍼 만들기(이것도 시작에 포함이 됌)
    void compute_buffer();
    void compute_descriptor();
    void compute_address_helper(GPUMeshBuffers* meshBuffer);

    //immdeiate submit(imgui 관련 메서드)
    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

    //전체적인 랜더링 작업
    //initializes everything in the engine
    void init();

    //shuts down the engine
    void cleanup();

    //draw loop
    void draw_background(VkCommandBuffer cmd);
    void draw_geomatry(VkCommandBuffer cmd);
    void compute_cull(VkCommandBuffer cmd);
    void draw();
    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

    //run main loop
    void run();
    private:
    void init_imgui();
};
