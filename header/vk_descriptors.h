#pragma once
#include <span>
#include <vulkan/vulkan.h>
#include <vector>
#include <deque>

struct DescriptorLayoutBuilder {

    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void add_binding(uint32_t binding, VkDescriptorType type);
    void clear();
    VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};

struct DescriptorAllocator {

    struct PoolSizeRatio{
	VkDescriptorType type;
	float ratio;
    };

    VkDescriptorPool pool;

    void init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
    void clear_descriptors(VkDevice device);
    void destroy_pool(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
};
//이제는 VkUpdateDescriptorSet으로 수동적으로 디스크립터를 수정을 하지 않을것이라는 선언
struct DescriptorWriter {
    std::deque<VkDescriptorImageInfo> imageInfos;
    std::deque<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet> writes;

    void write_image(int binding,VkImageView image,VkSampler sampler , VkImageLayout layout, VkDescriptorType type);
    void write_buffer(int binding,VkBuffer buffer,size_t size, size_t offset,VkDescriptorType type); 

    void clear();
    void update_set(VkDevice device, VkDescriptorSet set);
};

//이것은 만약 descriptor풀이 꽉차면 필요한 것이다
//늘려주는 코드
struct DescriptorAllocatorGrowable {
public:
    struct PoolSizeRatio {
	VkDescriptorType type;
	float ratio;
    };
    void init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios);
    void clear_pools(VkDevice device);
    void destroy_pools(VkDevice device);
    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext = nullptr);
private:
    //이것은 풀을 가지고 오는것 (readyPools, 아니면 새롭개 창조)
    VkDescriptorPool get_pool(VkDevice device);
    VkDescriptorPool create_pool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);

    //얼마나 많은 메모리를 할당할것인가 각각 pool에
    std::vector<PoolSizeRatio> ratios;
    //이미 쓸수 없는 풀
    std::vector<VkDescriptorPool> fullPools;
    //쓸 준비가 된 풀
    std::vector<VkDescriptorPool> readyPools;
    uint32_t setsPerPool;
};
