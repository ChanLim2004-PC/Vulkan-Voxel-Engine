#pragma once
#include "./vk_types.h"
#include "./vk_descriptors.h"

#include <unordered_map>
#include <filesystem>

//a connecter to MaterialInstance
struct GLTFMaterial {
    MaterialInstance data;
};

struct Bounds {
    glm::vec3 origin;
    float sphereRadius;
    glm::vec3 extents;
};

//submeshes inside main_mashes
struct GeoSurface {
    uint32_t startIndex;
    uint32_t count;
    Bounds bounds;
    std::shared_ptr<GLTFMaterial> material;
};

//the main_mashe it self(store vertex and index SSBO)
struct MeshAsset {
    std::string name;
    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers meshBuffers;
};


//전방 선언--> class VulkanEngine이 선언이 되기 전에 컴파일러한테 이런게 존재한다를 알려주는 코드
class VulkanEngine;

struct LoadedGLTF : public IRenderable {

    //메쉬(위치)(vertex, indcies)--> MashAsset 너가 vk_loader한번 보삼
    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
    //메인 메쉬들 --> Node(loadedNode)에 들어가는 실제 노드
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
    //택스쳐
    std::unordered_map<std::string, AllocatedImage> images;
    //재질(MaterialInstance가 저장이 되는곳)
    std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

    //일종에 매인 메쉬들의 모임들
    std::vector<std::shared_ptr<Node>> topNodes;

    //샘플러 색깔을 읽고 적용...
    std::vector<VkSampler> samplers;

    DescriptorAllocatorGrowable descriptorPool;
    AllocatedBuffer materialDataBuffer;
    VulkanEngine* creator;

    //RAII 이것은 전부 따로 지울 필요가 없음
    ~LoadedGLTF() { clearAll(); };
    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);
private:
    void clearAll();
};
std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(VulkanEngine* engine, std::string_view filePath);
