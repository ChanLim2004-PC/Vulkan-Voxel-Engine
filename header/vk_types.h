#pragma once

#include "fastgltf/types.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

//에러를 검사를 하는 코드다
#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;						\
        if (err) {                                                      \
             fmt::print("Detected Vulkan error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0) // 맨 마지막 줄에는 역슬래시를 안 붙여도 돼.

//image 관련 memory allocation
struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

//creating buffers
struct AllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
};

//blending 관리
enum class MaterialPass :uint8_t {
    MainColor,
    Transparent,
    Other
};

//mesh 파이프라인들 관리
struct MaterialPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
};
struct MaterialInstance {
    MaterialPipeline* pipeline;
    VkDescriptorSet materialSet;
    MaterialPass passType;
};

//그냥 규격 vertex
struct Vertex {
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};

struct ChunkSlot {
    glm::ivec3 chunkCoord;// 청크 그리드 위치 (예: 2, 0, -5)
    bool isDirty = true;// 지형 갱신 필요 여부
    bool isAir = false;// 빈 공기 청크 여부

    uint32_t slotIndex = 0; // SSBO 내 슬롯 번호
    uint32_t firstIndex = 0;//SSBO 정점 오프셋

    VkAccelerationStructureKHR blasHandle = VK_NULL_HANDLE;
    VkDeviceAddress blasAddress = 0;
};

struct GPUMeshBuffers {
    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    AllocatedBuffer indirectCmdBuffer;
    AllocatedBuffer globalOffsetBuffer;
    AllocatedBuffer batchOffsetBuffer;
    AllocatedBuffer drawCounterBuffer;
    AllocatedBuffer finalIndirectBuffer;
    AllocatedBuffer indirectRangeBuffer;
    //binding 과정 vulkan 1.0처럼 더이상 vkCmdBindVertexBuffers같은건 필요가 없어
    VkDeviceAddress vertexBufferAddress;
    VkDeviceAddress indexBufferAddress;
    VkDeviceAddress indirectCmdBufferAddress;
    VkDeviceAddress globalOffsetBufferAddress;
    VkDeviceAddress batchOffsetBufferAddress;
    VkDeviceAddress drawCounterBufferAddress;
    VkDeviceAddress finalIndirectBufferAddress;
    VkDeviceAddress indirectRangeBufferAddress;

    //blas

    AllocatedBuffer blasBuffer;
    VkAccelerationStructureKHR blasHandle;
    VkDeviceAddress blasDeviceAddress;

    AllocatedBuffer scratchBuffer;
};

struct RayTracingBuffer{
    AllocatedBuffer instanceBuffer;
    VkDeviceAddress instanceBufferAddress;

    VkDescriptorSetLayout tlsaBufferDescriptorLayout;
    VkDescriptorSet tlsaBufferDescriptor;
    VkAccelerationStructureKHR tlasHandle = VK_NULL_HANDLE;
    AllocatedBuffer tlsaBuffer;
    VkDeviceAddress tlsaBufferAddress;

    AllocatedBuffer scratchBuffer;
    VkDeviceAddress scratchBufferAddress;
};

struct SceneDataBuffer{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;
    glm::vec4 cameraPosition;
    glm::vec4 sunDirection;
    glm::vec4 sunColor;
};

struct RayTracerUniformBuffer{
    SceneDataBuffer sceneData;
    VkDescriptorSetLayout RayTracerDescriptorLayout;
    VkDescriptorSet RayTracerDescriptor;
    AllocatedBuffer RayTracerShaderBuffer;
};

struct MarchingCubeBuffer {
    std::array<int, 256> edgeTable;
    std::array<std::array<int, 16>, 256> triTable;
};

struct alignas(16) DrawMeshBuffer {
    glm::mat4 worldMatrix;
    VkDeviceAddress outputVertexBuffer;
};

// push constants for our mesh object draws(이제는 단순히 보내는것이 아닌 Output은 너다)
struct GPUDrawPushConstants {
    VkDeviceAddress outputVertexBuffer;
    VkDeviceAddress outputIndexBuffer;
    VkDeviceAddress indirectCmdBuffer;
    VkDeviceAddress globalOffsetCmdBuffer;
    VkDeviceAddress indirectRangeBuffer;
    uint64_t indirectBufferNum;    
    glm::vec4 starting_push_constant;
};

struct GPUCullData {
    glm::mat4 view;              
    glm::vec4 frustum;            
    float znear, zfar;           
    uint32_t drawCount;          
    int32_t cullingEnabled;      
    int32_t occlusionEnabled;    
    int32_t lodEnabled;         
    float pyramidWidth;          
    float pyramidHeight;        
};
//Culling할때 필요한 PushConstant
struct CullComputePushConstant{
    VkDeviceAddress finalIndirectBuffer;
    VkDeviceAddress indirectCmdBuffer;
    VkDeviceAddress batchOffsetBuffer;
    VkDeviceAddress drawCounterBuffer;
    uint64_t indirectBufferNum;    
    glm::vec4 starting_push_constant;
};

//fragmentShader을 위한 전용 버퍼
struct FragmentUniform{
    glm::vec4 screenSize;
};

struct DrawContext;

// base class for a renderable dynamic object
class IRenderable {

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

struct Node : public IRenderable {
    // parent pointer must be a weak pointer to avoid circular dependencies
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;
    glm::mat4 localTransform;
    glm::mat4 worldTransform;

    void refreshTransform(const glm::mat4& parentMatrix)
    {
        worldTransform = parentMatrix * localTransform;
        for (auto c : children) {
            c->refreshTransform(worldTransform);
        }
    }

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
    {
        // draw children
        for (auto& c : children) {
            c->Draw(topMatrix, ctx);
        }
    }
};
