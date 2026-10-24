#include <cstdint>
#include <vulkan/vulkan.h>
#include "../../header/raytracing.h"
#include "../../header/vk_engine.h"
#include "../../header/voxeldata.h"
#include "../../header/voxeldata.h"

void RayTracingTool::initRayTracingFunctions(VkDevice _device) {
    vkGetAccelerationStructureBuildSizesKHR_ = 
	(PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(_device, "vkGetAccelerationStructureBuildSizesKHR");

    vkCreateAccelerationStructureKHR_ = 
       (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(_device, "vkCreateAccelerationStructureKHR");

    vkGetAccelerationStructureDeviceAddressKHR_ = 
	(PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(_device, "vkGetAccelerationStructureDeviceAddressKHR");

    vkCmdBuildAccelerationStructuresKHR_ = 
        (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(_device, "vkCmdBuildAccelerationStructuresKHR");

    vkDestroyAccelerationStructureKHR_ = 
        (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(_device, "vkDestroyAccelerationStructureKHR");

    vkDestroyAccelerationStructureKHR_ = 
        (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(_device, "vkDestroyAccelerationStructureKHR");

    vkCmdBuildAccelerationStructuresIndirectKHR_ = 
	(PFN_vkCmdBuildAccelerationStructuresIndirectKHR)vkGetDeviceProcAddr(_device, "vkCmdBuildAccelerationStructuresIndirectKHR");
}

VkDeviceAddress get_buffer_device_address(VkDevice device, VkBuffer buffer) {
    VkBufferDeviceAddressInfo deviceAddressInfo{};
    deviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    deviceAddressInfo.pNext = nullptr;
    deviceAddressInfo.buffer = buffer;
    return vkGetBufferDeviceAddress(device, &deviceAddressInfo);
}

void RayTracingTool::accelerationBuffer(VulkanEngine* engine, VkCommandBuffer* cmd, RayTracingBuffer* rayTracingBuffer){
    //Buffer Descriptor 연결 및 write 그리고 read

    rayTracingBuffer->tlsaBufferDescriptor = engine->get_current_frame()._frameDescriptors.allocate(engine->_device, rayTracingBuffer->tlsaBufferDescriptorLayout);
    //TLAS BUFFER Write and Update
    VkWriteDescriptorSetAccelerationStructureKHR accelInfo{};
    accelInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelInfo.accelerationStructureCount = 1;
    accelInfo.pAccelerationStructures = &rayTracingBuffer->tlasHandle;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = &accelInfo;
    write.dstSet = rayTracingBuffer->tlsaBufferDescriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    vkUpdateDescriptorSets(engine->_device, 1, &write, 0, nullptr);
}

void RayTracingTool::bindDescriptorSet(VulkanEngine* engine, VkCommandBuffer* cmd, RayTracingBuffer* raytracingBuffer){
    vkCmdBindDescriptorSets(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, engine->_graphicsPipelineLayout, 4, 1, &raytracingBuffer->tlsaBufferDescriptor, 0, nullptr);
}

void RayTracingTool::createAccelerationBuffer(VulkanEngine* engine, ChunkSystem* system, RayTracingBuffer* rayTracingBuffer, VkAccelerationStructureBuildSizesInfoKHR* tlasBuildSizes){
    rayTracingBuffer->instanceBuffer = engine->create_buffer(
	sizeof(VkAccelerationStructureInstanceKHR) * system->indirect_buffer_size,
	VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	VMA_MEMORY_USAGE_CPU_TO_GPU
    );
    rayTracingBuffer->instanceBufferAddress = get_buffer_device_address(engine->_device, rayTracingBuffer->instanceBuffer.buffer);

    rayTracingBuffer->tlsaBuffer = engine->create_buffer(
	tlasBuildSizes->accelerationStructureSize,
	VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	VMA_MEMORY_USAGE_GPU_ONLY
    );
    rayTracingBuffer->tlsaBufferAddress = get_buffer_device_address(engine->_device, rayTracingBuffer->tlsaBuffer.buffer);

    rayTracingBuffer->scratchBuffer = engine->create_buffer(
	tlasBuildSizes->buildScratchSize + 256,
	VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	VMA_MEMORY_USAGE_GPU_ONLY
    ); 
    rayTracingBuffer->scratchBufferAddress = get_buffer_device_address(engine->_device, rayTracingBuffer->scratchBuffer.buffer);
    VkDeviceAddress rawScratch = rayTracingBuffer->scratchBufferAddress;
    VkDeviceAddress alignedScratch = (rawScratch + 255ULL) & ~255ULL;
    rayTracingBuffer->scratchBufferAddress = alignedScratch;
}

void RayTracingTool::createBLASStructure(VulkanEngine* engine, VkCommandBuffer* cmd, GPUMeshBuffers* meshBuffer, ChunkSystem* system){
    //일단은 가지고 와야 하는것 3가지
    uint32_t numChunks = system->indirect_buffer_size;
    uint32_t numDirtyChunk = system->dirty_chunk_count;
    uint32_t chunkMaxVertices = system->max_buffer;
    uint32_t maxPrimitiveCount = chunkMaxVertices / 3;

    VkAccelerationStructureGeometryTrianglesDataKHR trianglesData{};
    trianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    trianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    trianglesData.vertexData.deviceAddress = meshBuffer->vertexBufferAddress;
    trianglesData.vertexStride = sizeof(Vertex);
    //해당 청크 하나가 담당하는 maxVertex를 의미를 한다.
    trianglesData.maxVertex = chunkMaxVertices - 1;
    trianglesData.indexType = VK_INDEX_TYPE_NONE_KHR;

    VkAccelerationStructureGeometryKHR blasGeometry{};
    blasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    blasGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    blasGeometry.geometry.triangles = trianglesData;
    blasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    //BLAS를 어떻게 빌드를 할것인지
    VkAccelerationStructureBuildGeometryInfoKHR blasBuildGeometryInfo{};
    //BLAS를 만들것이다 자신있게 선언
    blasBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    blasBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    blasBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    blasBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    //참고로 이것은 삼각형 모음 배열 꼴랑  하나만 쓰겠다는 예기
    blasBuildGeometryInfo.geometryCount = 1;
    //우리가 위에서 썻던 도형(Geomatry)정보들
    blasBuildGeometryInfo.pGeometries = &blasGeometry;

    VkAccelerationStructureBuildSizesInfoKHR blasBuildSizes{};
    blasBuildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    //BLAS를 만드는데 필요한 Query
    vkGetAccelerationStructureBuildSizesKHR_(engine->_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blasBuildGeometryInfo, &maxPrimitiveCount, &blasBuildSizes);

    VkDeviceSize alignedBlasSize = (blasBuildSizes.accelerationStructureSize + 255ULL) & ~255ULL;
    VkDeviceSize alignedScratchSize = (blasBuildSizes.buildScratchSize + 255ULL) & ~255ULL;

    if(meshBuffer->blasBuffer.buffer == VK_NULL_HANDLE){
	//blas관련 변수들의 vector을 잡아주삼
	meshBuffer->blasHandles.resize(numChunks);
        meshBuffer->blasDeviceAddresses.resize(numChunks);

	meshBuffer->blasBuffer = engine->create_buffer(
	    alignedBlasSize * numDirtyChunk,
	    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY
	);
	//그냥 스크레치 버퍼 생성및 연결
	meshBuffer->scratchBuffer = engine->create_buffer(
	    (alignedScratchSize * numDirtyChunk) + 256,
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY
	);

	for(int i = 0; i < numDirtyChunk; i++){
	    //0번 offset을 설정을 한 blas bufferinfo 만들기
	    VkAccelerationStructureCreateInfoKHR blasCreateInfo{};
	    blasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	    blasCreateInfo.buffer = meshBuffer->blasBuffer.buffer;
	    blasCreateInfo.offset = i * alignedBlasSize;
	    blasCreateInfo.size = blasBuildSizes.accelerationStructureSize;
	    blasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	    //Vulkan이 알아드를 수 있는 핸들을 생성을 한다.
	    VkResult result = vkCreateAccelerationStructureKHR_(
		engine->_device, 
		&blasCreateInfo, 
		nullptr, 
		&meshBuffer->blasHandles[i]
	    );
	    //TLAS가 써야하는 blas주소들 획득하기
	    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
            addrInfo.accelerationStructure = meshBuffer->blasHandles[i];
            meshBuffer->blasDeviceAddresses[i] = vkGetAccelerationStructureDeviceAddressKHR_(engine->_device, &addrInfo);
	}

	//핸들을 스트럭트 안에다가 저장
	AllocatedBuffer tempBuffer = meshBuffer->blasBuffer;
	AllocatedBuffer tempBuffer2 = meshBuffer->scratchBuffer;
	auto tempBuffer3 = meshBuffer->blasHandles;
	engine->_mainDeletionQueue.push_function([this, engine, tempBuffer, tempBuffer2, tempBuffer3, numDirtyChunk](){
	    for(int i = 0; i < numDirtyChunk; i++){
		vkDestroyAccelerationStructureKHR_(engine->_device, tempBuffer3[i], nullptr);
	    }
	    engine->destroy_buffer(tempBuffer2);
	    engine->destroy_buffer(tempBuffer);
	});
    }
    // 필요한 모든 벡터를 numDirtyChunk 크기만큼만 할당
    std::vector<VkAccelerationStructureGeometryKHR> geoms(numDirtyChunk);
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos(numDirtyChunk);

    //실제 값들이 담겨 있음(Chunk하나당 삼각형의 갯수)
    std::vector<uint32_t> maxPrimitives(numDirtyChunk, maxPrimitiveCount);
    //각 원소의 주소값(uint32_t* 등)을 저장하는 포인터 전용 배열이다.
    std::vector<const uint32_t*> pMaxPrimitives(numDirtyChunk);
    std::vector<VkDeviceAddress> indirectDeviceAddresses(numDirtyChunk);

    uint32_t indirectStride = sizeof(VkAccelerationStructureBuildRangeInfoKHR);
    std::vector<uint32_t> indirectStrides(numDirtyChunk, indirectStride);

    VkDeviceAddress baseVertexAddr = meshBuffer->vertexBufferAddress;
    VkDeviceAddress scratchBase = (get_buffer_device_address(engine->_device, meshBuffer->scratchBuffer.buffer) + 255ULL) & ~255ULL;

    // 단 하나의 루프 안에서 dirtyIndex를 기준으로 모든 지시서 완성
    for (uint32_t i = 0; i < numDirtyChunk; ++i) {
	uint32_t dirtyIndex = system->dirtyChunkIndices[i];

	// 포인터 연결
	pMaxPrimitives[i] = &maxPrimitives[i];

	// 정점 VRAM 주소
	VkDeviceAddress chunkVertexAddr = baseVertexAddr + (dirtyIndex * chunkMaxVertices * sizeof(Vertex));
	geoms[i] = blasGeometry;
	geoms[i].geometry.triangles.vertexData.deviceAddress = chunkVertexAddr;

	// 빌드 정보 작성
	buildInfos[i].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfos[i].pNext = nullptr;
	buildInfos[i].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfos[i].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfos[i].flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
	buildInfos[i].geometryCount = 1;
	buildInfos[i].pGeometries = &geoms[i];
	buildInfos[i].dstAccelerationStructure = meshBuffer->blasHandles[dirtyIndex];
	buildInfos[i].scratchData.deviceAddress = scratchBase + (i * alignedScratchSize);

	// i번째 지시서에 dirtyIndex 슬롯의 Range 주소를 정확히 매핑
	indirectDeviceAddresses[i] = meshBuffer->indirectRangeBufferAddress + (dirtyIndex * indirectStride);
    }
    // GPU에 딱 numDirtyChunk 개수만큼 제출
    vkCmdBuildAccelerationStructuresIndirectKHR_(*cmd,numDirtyChunk,buildInfos.data(),indirectDeviceAddresses.data(),indirectStrides.data(),pMaxPrimitives.data());
}

void RayTracingTool::createTLAStructure(VulkanEngine* engine, VkCommandBuffer* cmd, ChunkSystem* system, GPUMeshBuffers* meshBuffer, RayTracingBuffer* raytracingBuffer){
    //이것은 나중에 필요할것이다 하지만 지금은 필요없으니 봉인을 한다.
    //std::vector<VkAccelerationStructureInstanceKHR> instances;

    // 인스턴스 데이터 명세 (좌표 명세서)
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = raytracingBuffer->instanceBufferAddress;

    // TLAS 지오메트리 명세서 작성
    VkAccelerationStructureGeometryKHR tlasGeometry{};
    tlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR; 
    tlasGeometry.geometry.instances = instancesData;
    tlasGeometry.flags = 0;

    //TLAS Info 계속 만들기
    VkAccelerationStructureBuildGeometryInfoKHR tlasBuildGeometryInfo{};
    tlasBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tlasBuildGeometryInfo.pNext = nullptr;
    //BLAS를 만들 때는 BOTTOM_LEVEL이었지만, 이번에는 최상위 가속 구조이므로 TOP_LEVEL로 설정함
    tlasBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; 
    //GPU 드라이버에게 "빌드 시간이 조금 더 걸리더라도 광선 추적(TraceRay) 속도가 가장 빠르도록 트리를 촘촘하게 최적화해 달라"고 힌트를 줌.
    tlasBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR; 
    //기존 가속 구조를 살짝 갱신하는 것(UPDATE)이 아니라 처음부터 완전히 새로 굽는(BUILD) 모드임.
    tlasBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; 
    tlasBuildGeometryInfo.geometryCount = 1;
    tlasBuildGeometryInfo.pGeometries = &tlasGeometry;

    // 나중에 필요할 수도 있다.
    // tlasBuildGeometryInfo.scratchData.deviceAddress = scratchBufferAddress;
    
    //메모리 정보  Query설정
    VkAccelerationStructureBuildSizesInfoKHR tlasBuildSizes{};
    tlasBuildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    tlasBuildSizes.pNext = nullptr;
    //현제 우리의 instance는 한개(BLAS가 에초에 한개임)
    uint32_t maxInstanceCount = system->indirect_buffer_size;

    // TLAS에 필요한 VRAM 크기 및 스크래치 버퍼 크기를 뱉음 (나중에 VkBuffer을 만들때 쓰삼)
    // output--> tlasBuildSizes 
    vkGetAccelerationStructureBuildSizesKHR_(engine->_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlasBuildGeometryInfo, &maxInstanceCount, &tlasBuildSizes);

    //세상에 존재하는 모든 BLAS들을 총괄하는 '월드 전체 지도'이다. 전체 청크만큼 필요하다.
    VkTransformMatrixKHR identityMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };

    // TLAS 생성 명세서 및 버퍼 연결
    if(raytracingBuffer->tlasHandle == VK_NULL_HANDLE){
	createAccelerationBuffer(engine, system, raytracingBuffer, &tlasBuildSizes);
	VkAccelerationStructureCreateInfoKHR tlasCreateInfo{};
	tlasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	tlasCreateInfo.pNext = nullptr;
	tlasCreateInfo.buffer = raytracingBuffer->tlsaBuffer.buffer; 
	tlasCreateInfo.offset = 0;                 
	tlasCreateInfo.size = tlasBuildSizes.accelerationStructureSize; 
	tlasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; 
	// TLAS 핸들 생성
	vkCreateAccelerationStructureKHR_(
	    engine->_device,
	    &tlasCreateInfo,
	    nullptr,
	    &raytracingBuffer->tlasHandle
	);
	AllocatedBuffer tempBuffer1 = raytracingBuffer->scratchBuffer;
	AllocatedBuffer tempBuffer2 = raytracingBuffer->tlsaBuffer;
	AllocatedBuffer tempBuffer3 = raytracingBuffer->instanceBuffer;
	VkAccelerationStructureKHR tempBuffer4 = raytracingBuffer->tlasHandle;

	engine->_mainDeletionQueue.push_function([this, engine, tempBuffer1, tempBuffer2, tempBuffer3, tempBuffer4](){
	    vkDestroyAccelerationStructureKHR_(engine->_device, tempBuffer4, nullptr);
	    engine->destroy_buffer(tempBuffer1);
	    engine->destroy_buffer(tempBuffer2);
	    engine->destroy_buffer(tempBuffer3);
	});

	std::vector<VkAccelerationStructureInstanceKHR> instances(system->indirect_buffer_size);
	for (uint32_t i = 0; i < system->indirect_buffer_size; ++i) {
	    instances[i].transform = identityMatrix;
	    instances[i].instanceCustomIndex = i; // 청크 ID를 셰이더에서 gl_InstanceCustomIndexEXT로 조회 가능
	    instances[i].mask = 0xFF;             // 광선 추돌 마스크 (모두 활성화)
	    instances[i].instanceShaderBindingTableRecordOffset = 0;
	    // BackFace Culling은 지금 끄겠다
	    instances[i].flags = 0;
	    // BLAS주소를 넘겨준다.
	    instances[i].accelerationStructureReference = meshBuffer->blasDeviceAddresses[i];
	}

	void* mappedInstanceData = nullptr;
	vmaMapMemory(engine->_allocator, raytracingBuffer->instanceBuffer.allocation, &mappedInstanceData);
	memcpy(mappedInstanceData, instances.data(), sizeof(VkAccelerationStructureInstanceKHR) * system->indirect_buffer_size);
	vmaFlushAllocation(engine->_allocator, raytracingBuffer->instanceBuffer.allocation, 0, VK_WHOLE_SIZE); 
	vmaUnmapMemory(engine->_allocator, raytracingBuffer->instanceBuffer.allocation);
	// Host Write -> GPU Read 메모리 배리어
	VkMemoryBarrier2 hostToAsBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
	hostToAsBarrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
	hostToAsBarrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
	hostToAsBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	hostToAsBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	VkDependencyInfo hostDep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 1, &hostToAsBarrier };
	vkCmdPipelineBarrier2(*cmd, &hostDep);
    }
    instancesData.data.deviceAddress = raytracingBuffer->instanceBufferAddress;
    tlasGeometry.geometry.instances = instancesData;
       
    //TLAS 빌드 범위 설정 (TLAS에서 primitiveCount는 인스턴스 개수)
    VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo{};
    tlasRangeInfo.primitiveCount = system->indirect_buffer_size; 
    tlasRangeInfo.primitiveOffset = 0; 
    tlasRangeInfo.firstVertex = 0;
    tlasRangeInfo.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* pTlasRangeInfos = &tlasRangeInfo;

    //BLAS처럼 스크에치버퍼도 함께 연결
    tlasBuildGeometryInfo.scratchData.deviceAddress = raytracingBuffer->scratchBufferAddress;
    tlasBuildGeometryInfo.pGeometries = &tlasGeometry;
    // 빌드 명세서에 방금 만든 TLAS 핸들을 목적지(dst)로 등록
    tlasBuildGeometryInfo.dstAccelerationStructure = raytracingBuffer->tlasHandle;

    //TLAS를 실제로 구워
    vkCmdBuildAccelerationStructuresKHR_(
	*cmd,
	1,                      
	&tlasBuildGeometryInfo, 
	&pTlasRangeInfos 
    );
}
void RayTracingTool::activateRayTacing(VulkanEngine* engine, ChunkSystem* system, VkCommandBuffer* cmd, GPUMeshBuffers* meshBuffer, RayTracingBuffer* raytracingBuffer){
    if (engine->_chunkIntialized){
	//computeshader들이 다 끝날때까지 대기를 시킴
	//computeShader(끝날때가지만) --> BLAS빌드(대기)
	VkMemoryBarrier2 computeToAsBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
	computeToAsBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	computeToAsBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
	computeToAsBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	computeToAsBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT;
	VkDependencyInfo dep1{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dep1.memoryBarrierCount = 1;
	dep1.pMemoryBarriers = &computeToAsBarrier;
	vkCmdPipelineBarrier2(*cmd, &dep1);

	createBLASStructure(engine, cmd, meshBuffer, system);

	//BLAS이 다 끝날때까지 대기를 시킴
	//BLAS빌드(끝날때가지만) --> TLAS빌드(대기)
	VkMemoryBarrier2 blasToTlasBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
	blasToTlasBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	blasToTlasBarrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	blasToTlasBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	blasToTlasBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	VkDependencyInfo dep2{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dep2.memoryBarrierCount = 1;
	dep2.pMemoryBarriers = &blasToTlasBarrier;
	vkCmdPipelineBarrier2(*cmd, &dep2);

	createTLAStructure(engine, cmd, system, meshBuffer, raytracingBuffer);

	VkMemoryBarrier2 tlasToShaderBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
	tlasToShaderBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	tlasToShaderBarrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	tlasToShaderBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	tlasToShaderBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT;
	VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 1, &tlasToShaderBarrier };
	vkCmdPipelineBarrier2(*cmd, &dep);
	engine->_chunkIntialized = false;
    }
    accelerationBuffer(engine, cmd, raytracingBuffer);
}
