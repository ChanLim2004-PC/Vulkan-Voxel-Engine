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
#include <iostream>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

//에러를 검사를 하는 코드다
#define VK_CHECK(x) \
    do { \
        VkResult err = x; \
        if (err) { \
            std::cerr << "\n VK_CHECK FAILED! Error Code: " << (int)err \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            abort(); \
        } \
    } while (0)

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

struct ChunkMetaData {
    VkDrawIndirectCommand drawCmd;    
    VkAccelerationStructureBuildRangeInfoKHR blasRange;  
    glm::vec4 aabbMin;
    glm::vec4 aabbMax;
    uint32_t chunkID;
    //1은 dirty, 0은 air
    uint32_t flags;      
    uint32_t _pad[2];
};

struct GPUMeshBuffers {
    AllocatedBuffer vertexBuffer;
    AllocatedBuffer prevCordBuffer;
    AllocatedBuffer indexBuffer;
    AllocatedBuffer drawCounterBuffer;
    AllocatedBuffer finalIndirectBuffer;
    AllocatedBuffer chunkMetaBuffer;
    AllocatedBuffer indirectRangeBuffer;

    VkDeviceAddress vertexBufferAddress;
    VkDeviceAddress prevCordBufferAddress;
    VkDeviceAddress indexBufferAddress;
    VkDeviceAddress drawCounterBufferAddress;
    VkDeviceAddress finalIndirectBufferAddress;
    VkDeviceAddress chunkMetaBufferAddress;
    VkDeviceAddress indirectRangeBufferAddress;

    //blas(not going to use right now)
    std::vector<VkAccelerationStructureKHR> blasHandles;
    std::vector<VkDeviceAddress> blasDeviceAddresses;
    AllocatedBuffer blasBuffer;
    AllocatedBuffer scratchBuffer;
    //voxelCount
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

struct MarchingCubeBuffer {
    std::array<int, 256> edgeTable;
    std::array<std::array<int, 16>, 256> triTable;
};

struct alignas(16) DrawMeshBuffer {
    glm::mat4 worldMatrix;
    VkDeviceAddress outputVertexBuffer;
};

struct VoxelCountPushConstants {
    VkDeviceAddress chunkMetaBuffer;
    VkDeviceAddress prevCordBuffer;
    glm::vec4 inputStartPosition;
    glm::uvec4 chunkGridDim;
};

struct PrefixPushConstants{
    VkDeviceAddress chunkMetaBuffer;
    uint32_t totalChunkCount;
    uint32_t _pad;
};
// push constants for our mesh object draws(이제는 단순히 보내는것이 아닌 Output은 너다)
struct GPUDrawPushConstants {
    VkDeviceAddress outputVertexBuffer;
    VkDeviceAddress chunlMetaData;
    VkDeviceAddress rangeBuffer;
    uint32_t _pad[2];
    glm::vec4 starting_push_constant;
    glm::uvec4 girdDim;
};
//Culling할때 필요한 PushConstant
struct CullComputePushConstant{
    VkDeviceAddress finalIndirectBuffer;
    VkDeviceAddress ChunkMetaDataBuffer;
    VkDeviceAddress drawCounterBuffer;
    uint32_t total_chunk_count;
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
