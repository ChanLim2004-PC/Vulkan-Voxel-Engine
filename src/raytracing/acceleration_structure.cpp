#include <vulkan/vulkan.h>
#include "../../header/raytracing.h"
#include "../../header/vk_engine.h"
#include "../../header/voxeldata.h"
#include <iostream>

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
    vkCmdBindDescriptorSets(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, engine->_graphicsPipelineLayout, 4, 1, &rayTracingBuffer->tlsaBufferDescriptor, 0, nullptr);
}

void RayTracingTool::createAccelerationBuffer(VulkanEngine* engine, RayTracingBuffer* rayTracingBuffer, VkAccelerationStructureBuildSizesInfoKHR* tlasBuildSizes){
    rayTracingBuffer->instanceBuffer = engine->create_buffer(
	sizeof(VkAccelerationStructureInstanceKHR),
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

void RayTracingTool::createBLASStructure(VulkanEngine* engine, VkCommandBuffer* cmd, GPUMeshBuffers* meshBuffer, int BLAS_vertcies_size){
    VkAccelerationStructureGeometryTrianglesDataKHR trianglesData{};
    trianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    trianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    trianglesData.vertexData.deviceAddress = meshBuffer->vertexBufferAddress;
    trianglesData.vertexStride = sizeof(Vertex);
    //compute_buffer에서부터 가지고 오면 된다.
    trianglesData.maxVertex = BLAS_vertcies_size - 1;
    trianglesData.indexType = VK_INDEX_TYPE_UINT32;
    trianglesData.indexData.deviceAddress = meshBuffer->indexBufferAddress;

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
    //이제 얼마나 많은 Triangle(primitive)가 버퍼 안에 있는지....
    uint32_t maxPrimitiveCount = BLAS_vertcies_size/3;

    //BLAS를 만드는데 필요한 Query
    vkGetAccelerationStructureBuildSizesKHR_(engine->_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blasBuildGeometryInfo, &maxPrimitiveCount, &blasBuildSizes);
    if(meshBuffer->blasBuffer.buffer == VK_NULL_HANDLE){
	meshBuffer->blasBuffer = engine->create_buffer(
	    blasBuildSizes.accelerationStructureSize,
	    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY
	);
	//0번 offset을 설정을 한 blas bufferinfo 만들기
	VkAccelerationStructureCreateInfoKHR blasCreateInfo{};
	blasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	blasCreateInfo.buffer = meshBuffer->blasBuffer.buffer;
	blasCreateInfo.offset = 0;
	blasCreateInfo.size = blasBuildSizes.accelerationStructureSize;
	blasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	//Vulkan이 알아드를 수 있는 핸들을 생성을 한다.
	VkResult result = vkCreateAccelerationStructureKHR_(
	    engine->_device, 
	    &blasCreateInfo, 
	    nullptr, 
	    &meshBuffer->blasHandle
	);

	//그냥 스크레치 버퍼 생성및 연결
	meshBuffer->scratchBuffer = engine->create_buffer(
	    blasBuildSizes.buildScratchSize + 256,
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY
	);

	//핸들을 스트럭트 안에다가 저장
	AllocatedBuffer tempBuffer = meshBuffer->blasBuffer;
	AllocatedBuffer tempBuffer2 = meshBuffer->scratchBuffer;
	VkAccelerationStructureKHR tempBuffer3 = meshBuffer->blasHandle;
	engine->_mainDeletionQueue.push_function([this, engine, tempBuffer, tempBuffer2, tempBuffer3](){
	    vkDestroyAccelerationStructureKHR_(engine->_device, tempBuffer3, nullptr);
	    engine->destroy_buffer(tempBuffer2);
	    engine->destroy_buffer(tempBuffer);
	});
    }

    VkDeviceAddress scratchAddress = get_buffer_device_address(engine->_device, meshBuffer->scratchBuffer.buffer);
    VkDeviceAddress rawScratch = scratchAddress;
    VkDeviceAddress alignedScratch = (rawScratch + 255ULL) & ~255ULL;

    blasBuildGeometryInfo.dstAccelerationStructure = meshBuffer->blasHandle;
    blasBuildGeometryInfo.scratchData.deviceAddress = alignedScratch;

    const uint32_t* pMaxPrimitiveCount = &maxPrimitiveCount;

    uint32_t indirectStride = sizeof(VkAccelerationStructureBuildRangeInfoKHR);
    vkCmdBuildAccelerationStructuresIndirectKHR_(*cmd, 1, &blasBuildGeometryInfo, &meshBuffer->indirectRangeBufferAddress, &indirectStride, &pMaxPrimitiveCount);
}

void RayTracingTool::createTLAStructure(VulkanEngine* engine, VkCommandBuffer* cmd, GPUMeshBuffers* meshBuffer, RayTracingBuffer* raytracingBuffer){
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
    tlasBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR; 
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
    uint32_t maxInstanceCount = 1;

    // TLAS에 필요한 VRAM 크기 및 스크래치 버퍼 크기를 뱉음 (나중에 VkBuffer을 만들때 쓰삼)
    // output--> tlasBuildSizes 
    vkGetAccelerationStructureBuildSizesKHR_(engine->_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlasBuildGeometryInfo, &maxInstanceCount, &tlasBuildSizes);

    // TLAS 생성 명세서 및 버퍼 연결
    if(raytracingBuffer->tlasHandle == VK_NULL_HANDLE){
	createAccelerationBuffer(engine, raytracingBuffer, &tlasBuildSizes);
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
    }

    // 방금 우리는 BLAS Handle를 만들었다 그리고 이 주소를 조회하는 과정
    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = meshBuffer->blasHandle;
    VkDeviceAddress blasDeviceAddr = vkGetAccelerationStructureDeviceAddressKHR_(engine->_device, &addrInfo);
    VkAccelerationStructureInstanceKHR instance{}; VkTransformMatrixKHR transformMatrix = {
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f
    };

    instance.transform = transformMatrix;
    instance.instanceCustomIndex = 0;
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    // BackFace Culling은 지금 끄겠다
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    // BLAS주소를 넘겨준다.
    instance.accelerationStructureReference = blasDeviceAddr;
    //이것도 필요한 날이 올것이다 하지만 지금은 봉인
    //instances.push_back(instance);
    
    void* mappedInstanceData = nullptr;
    vmaMapMemory(engine->_allocator, raytracingBuffer->instanceBuffer.allocation, &mappedInstanceData);
    memcpy(mappedInstanceData, &instance, sizeof(VkAccelerationStructureInstanceKHR));
    vmaUnmapMemory(engine->_allocator, raytracingBuffer->instanceBuffer.allocation);

    tlasGeometry.geometry.instances.data.deviceAddress = raytracingBuffer->instanceBufferAddress;

    //TLAS 빌드 범위 설정 (TLAS에서 primitiveCount는 인스턴스 개수)
    VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo{};
    tlasRangeInfo.primitiveCount = 1; 
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
void RayTracingTool::activateRayTacing(VulkanEngine* engine, VkCommandBuffer* cmd, GPUMeshBuffers* meshBuffer, RayTracingBuffer* raytracingBuffer, int BLAS_vertcies_size){
    if (engine->_chunkFinishedInitilized){
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

	createBLASStructure(engine, cmd, meshBuffer, BLAS_vertcies_size);

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
	engine->_chunkFinishedInitilized = false;
    }
    createTLAStructure(engine, cmd, meshBuffer, raytracingBuffer);
    accelerationBuffer(engine, cmd, raytracingBuffer);
}
