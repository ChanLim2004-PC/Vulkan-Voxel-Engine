#include "../../header/vk_descriptors.h"
#include "../../header/vk_engine.h"

//일단 descriptors layout를 바인드를 할거임
//layout 어디에 바인딩이 될껀지 정함
//layout(rgba16f,set = 0, binding = 0) uniform image2D image;
void DescriptorLayoutBuilder::add_binding(uint32_t binding, VkDescriptorType type)
{
    VkDescriptorSetLayoutBinding newbind {};
    newbind.binding = binding;
    newbind.descriptorCount = 1;
    newbind.descriptorType = type;

    //연결을 한 descriptors는 이제 bindings라는 array안으로 들어가는거임
    bindings.push_back(newbind);
}

//이거는 걍 지우기
void DescriptorLayoutBuilder::clear()
{
    bindings.clear();
}

//실제 descriptors를 재작
VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags)
{
    //flags는 단순하다 그냥 모든 descriptors는 똑같은 flags를 갖는거다
    for (auto& b : bindings) {
        b.stageFlags |= shaderStages;
    }

    //여기는 info이므로 필요한 내용들을 그냥 삽입한다
    VkDescriptorSetLayoutCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.pNext = pNext;

    info.pBindings = bindings.data();
    info.bindingCount = (uint32_t)bindings.size();
    info.flags = flags;

    //세트로 묶어서 return을 한다
    VkDescriptorSetLayout set;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

    return set;
}

void DescriptorAllocator::init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
    //descriptors가 있을 pool을 생성(여러 풀들을 생성을 한다)
    std::vector<VkDescriptorPoolSize> poolSizes;
    //각각 풀 마다 이제 Descriptor 세트들을 데입을 한다
    for (PoolSizeRatio ratio : poolRatios) {
        poolSizes.push_back(VkDescriptorPoolSize{
            .type = ratio.type,
            .descriptorCount = uint32_t(ratio.ratio * maxSets)
        });
    }

    //풀info및 마들기
    VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.flags = 0;
    pool_info.maxSets = maxSets;
    pool_info.poolSizeCount = (uint32_t)poolSizes.size();
    pool_info.pPoolSizes = poolSizes.data();

    vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
}

//이제 실제로 vkPool 안에 메모리에다가 할당을 하는 것이다 VkDescriptor들을 
VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.pNext = nullptr;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    //GPU 드라이버가 풀 내부의 메모리 오프셋을 싹둑 잘라서 ds에 핸들(주소)을 쥐여줌
    VkDescriptorSet ds;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));

    //이제 set핸들은 여기에 있다 ds
    return ds;
}
//디스크립터 세트를 바인딩을 해서 쉐이더와 버퍼(텍스쳐)가 서로 소통을 할 수 있게끔 새팅만 하는거임
void DescriptorWriter::write_buffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type)
{
    //이미 Vram에 만들어진 VkBuffer과 GPU Shader이 서로 소통할수 있게끔 만드는 "열쇠"라고 생각 하면 편함
    VkDescriptorBufferInfo& info = bufferInfos.emplace_back(VkDescriptorBufferInfo{
	.buffer = buffer,
	.offset = offset,
	.range = size
    });

    //write는 그냥 디스크립터를 어떻게 GPU와 VkBuffer하고 통신을 시킬거냐
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstBinding = binding;   // 쉐이더(GPU) 안의 몇 번 바인딩 구역에 꽂을 건가? (binding = 0)
    write.dstSet = VK_NULL_HANDLE;// 일단 지금은 비워두고 나중에 쓸 때 채우겠다
    write.descriptorCount = 1;  
    write.descriptorType = type;  // 이게 유니폼 버퍼인지, 텍스처인지 종류를 명시!
    write.pBufferInfo = &info;    // 아까 위에서 만든 버퍼 안내 포스트잇(`&info`)

    writes.push_back(write);      // 이 명령서(`write`)를 일단 리스트에 차곡차곡 모아둔다
}

//위에는 단순한 버퍼 였다면 여기서는 이제 이미지임
void DescriptorWriter::write_image(int binding,VkImageView image, VkSampler sampler,  VkImageLayout layout, VkDescriptorType type)
{
    //이것은 VkImage와 GPU Shader하고의 소통 "열쇄"
    VkDescriptorImageInfo& info = imageInfos.emplace_back(VkDescriptorImageInfo{
	//smapler이미 한 내용이기는 하지만--> 이것은 SHADER이 이미지를 어떻게 읽어야 하는지
	.sampler = sampler,
	.imageView = image,
	.imageLayout = layout
    });

    VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

    write.dstBinding = binding;
    write.dstSet = VK_NULL_HANDLE; //left empty for now until we need to write it
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pImageInfo = &info;

    writes.push_back(write);
}
VkDescriptorPool DescriptorAllocatorGrowable::get_pool(VkDevice device){       
    //새로운 풀
    VkDescriptorPool newPool;
    //만약 readyPools가 비워져 있지 않고 뭔가 있어
    if (readyPools.size() != 0) {
	//readyPools의 가장 뒷부분에 있는 descriptorPool을 꺼내와
	newPool = readyPools.back();
	readyPools.pop_back();
    }
    else {
	//이거는 어쩔수 없이 새로 만들어야 함
	newPool = create_pool(device, setsPerPool, ratios);

	//풀의 사이즈를 약간 퀴워가지고 일단 재출을 하삼
	setsPerPool = setsPerPool * 1.5;
	//근데 만약 디스크립터 새트가 4092개가 넘어가면 그냥 4092로 맞춰 (GPU터질라)
	if (setsPerPool > 4092) {
	    setsPerPool = 4092;
	}
    }   
    return newPool;
}

VkDescriptorPool DescriptorAllocatorGrowable::create_pool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios){
    //여러개의 풀 크기를 백터에 저장을 한다.
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (PoolSizeRatio ratio : poolRatios) {
	poolSizes.push_back(VkDescriptorPoolSize{
	    .type = ratio.type,
	    //ratio.ratio의 의미는 만약 UBO를 만들면 1.0 * 총 필요한 세트 수
	    //만약 Texture이라면 ratio.ratio가 아마 3.0일거임
	    //즉 얼마나 많은 디스크립터 세트 및 그 세트의 크기를 정하는 거임
	    .descriptorCount = uint32_t(ratio.ratio * setCount)
	});
    }

    VkDescriptorPoolCreateInfo pool_info = {};
    //그거에 맞춰서 데이터를 넣어주면 끝이다
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = 0;
    pool_info.maxSets = setCount;
    pool_info.poolSizeCount = (uint32_t)poolSizes.size();
    pool_info.pPoolSizes = poolSizes.data();

    //긜고 선언을 한 빈 newPool에다가 방금 info들을 다 떄려박아주면 완성이다
    VkDescriptorPool newPool;
    vkCreateDescriptorPool(device, &pool_info, nullptr, &newPool);
    return newPool;
}

void DescriptorAllocatorGrowable::init(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
    //일단은 다 지우고 시작을 한다. 풀들의 모임들을
    ratios.clear();
    //각각 vector에다가 PoolSizeRatio라는 struct를 집어 넣음 나중에
    for (auto r : poolRatios) {
        ratios.push_back(r);
    }
    //생성을 해준다 (하나의 풀에 여러개의 PoolSizeRatio를 가질 수 있다)
    //참고로 PoolSizeRatio는 그냥 너에 풀의 대한 정보들이다.
    VkDescriptorPool newPool = create_pool(device, maxSets, poolRatios);

    setsPerPool = maxSets * 1.5;

    //지금 비어 있고 준비가 된 풀들이니 넣어준다
    readyPools.push_back(newPool);
}
void DescriptorAllocatorGrowable::clear_pools(VkDevice device)
{ 
    for (auto p : readyPools) {
        vkResetDescriptorPool(device, p, 0);
    }
    for (auto p : fullPools) {
        vkResetDescriptorPool(device, p, 0);
        readyPools.push_back(p);
    }
    fullPools.clear();
}
//마지막은 allocation function 여기서 메모리 할당이 일어남
VkDescriptorSet DescriptorAllocatorGrowable::allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext)
{
    //현제 사용가능한 풀을 가지고 오기
    VkDescriptorPool poolToUse = get_pool(device);

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.pNext = pNext;
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = poolToUse;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet ds;
	//풀에다가 descriptorSet공간을 allocate를 해줌
	VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &ds);

    //allocation failed. Try again
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {

	//꽉 찼다는 의미이므로 fullPools에다가 넣어줌
        fullPools.push_back(poolToUse);
	//그리고 다시 뽑기
        poolToUse = get_pool(device);
        allocInfo.descriptorPool = poolToUse;
       VK_CHECK( vkAllocateDescriptorSets(device, &allocInfo, &ds));
    }
    //메모리가 야무지게 할당이 됬으니 다시 넣어두삼
    readyPools.push_back(poolToUse);
    return ds;
}
void DescriptorAllocatorGrowable::destroy_pools(VkDevice device)
{
    for (auto p : readyPools) {
	vkDestroyDescriptorPool(device, p, nullptr);
    }
    readyPools.clear();
    for (auto p : fullPools) {
	vkDestroyDescriptorPool(device,p,nullptr);
    }
    fullPools.clear();
}
//이것은 단순히 이제 VKImage혹은 VkBuffer의 데이터들을 vkUpdateDescriptorSets 를 통해 쉐이더로 보내는 과정이다
void DescriptorWriter::update_set(VkDevice device, VkDescriptorSet set)
{
    for (VkWriteDescriptorSet& write : writes) {
        write.dstSet = set;
    }

    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
}
void DescriptorAllocator::clear_descriptors(VkDevice device)
{
    vkResetDescriptorPool(device, pool, 0);
}
void DescriptorAllocator::destroy_pool(VkDevice device)
{
    vkDestroyDescriptorPool(device,pool,nullptr);
}
void DescriptorWriter::clear()
{
    imageInfos.clear();
    writes.clear();
    bufferInfos.clear();
}
