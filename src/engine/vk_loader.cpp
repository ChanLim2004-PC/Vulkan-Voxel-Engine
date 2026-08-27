#include "fmt/base.h"
#define GLM_ENABLE_EXPERIMENTAL

#include "../../header/vk_loader.h"
#include "../../header/vk_types.h"
#include "../../header/vk_engine.h"

#include <glm/gtx/quaternion.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <iostream>
#include "stb_image.h"

VkFilter extract_filter(fastgltf::Filter filter)
{
    //이거는 샘플러 관련이 된 함수이다
    switch (filter) {
    //그냥 확장을 했을떄 날카롭게 확장을 할것이냐 아니면 부드럽게 확장을 할것이냐??
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
        return VK_FILTER_NEAREST;

    case fastgltf::Filter::Linear:
    case fastgltf::Filter::LinearMipMapNearest:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter)
{
    //여기는 뚝뚝 끊겨버림 0층과 1층이
    switch (filter) {
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::LinearMipMapNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;

    //mipmap 0층과 1층을 부드럽게 연결을 해줘
    case fastgltf::Filter::NearestMipMapLinear:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

std::optional<AllocatedImage> load_image(VulkanEngine* engine, fastgltf::Asset& asset, fastgltf::Image& image)
{
    AllocatedImage newImage {};

    int width, height, nrChannels;

    std::visit(
	fastgltf::visitor {
	    [](auto& arg) {},
	    [&](fastgltf::sources::URI& filePath) {
		//많은건 알필요 없고 그냥 외부 png, jpeg파일들을 가지고 오는것이다
		assert(filePath.fileByteOffset == 0); 
		assert(filePath.uri.isLocalPath()); 

		const std::string path(filePath.uri.path().begin(),
			 filePath.uri.path().end());
		//여기 stbi_load이 작업을 하는것이다 ㅇㅇ
		unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);

		//여기는 이미지를 만드는 것이다
		if (data) {
		    VkExtent3D imagesize;
		    imagesize.width = width;
		    imagesize.height = height;
		    imagesize.depth = 1;

		    newImage = engine->create_image(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT,false);

		    stbi_image_free(data);
		}
	    },
	    [&](fastgltf::sources::Vector& vector) {
		//gltf파일을 읽다보면.. 백터 형식으로 이미지가 저장이 된다(vector.bytes.data()) 여기에 바이너리 형식으로 이미지가 "하나" 저장이 된다
		//그러므로 파일 경로를 뒤질 필요 없이, 메모리에 있는 데이터를 그대로 stbi_load_from_memory()로 긁어와서 압축을 풀어
		unsigned char* data = stbi_load_from_memory(vector.bytes.data(), static_cast<int>(vector.bytes.size()),
					      &width, &height, &nrChannels, 4);
		//이미지 만들기
		if (data) {
		    VkExtent3D imagesize;
		    imagesize.width = width;
		    imagesize.height = height;
		    imagesize.depth = 1;

		    newImage = engine->create_image(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT,false);
		    stbi_image_free(data);
		}
	    },
	    [&](fastgltf::sources::BufferView& view) {
		//모델 데이터(점 위치, 인덱스 등)랑 이미지 데이터가 하나의 거대한 바이너리 통짜 파일(Buffer) 안에 다 비빔밥처럼 섞여 있는 경우지.
		auto& bufferView = asset.bufferViews[view.bufferViewIndex];
		auto& buffer = asset.buffers[bufferView.bufferIndex];

		std::visit(fastgltf::visitor { // We only care about VectorWithMime here, because we
		    [](auto& arg) {},
		    [&](fastgltf::sources::Vector& vector) {
			//거기서 점위치나 인덱스는 읽지 않고 일단은 이미지 데이터만 가지고 오는거임
			unsigned char* data = stbi_load_from_memory(vector.bytes.data() + bufferView.byteOffset,
					       static_cast<int>(bufferView.byteLength),
					       &width, &height, &nrChannels, 4);
			if (data) {
			    VkExtent3D imagesize;
			    imagesize.width = width;
			    imagesize.height = height;
			    imagesize.depth = 1;

			    newImage = engine->create_image(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM,
				       VK_IMAGE_USAGE_SAMPLED_BIT,false);

			    stbi_image_free(data);
			}
		    } },
	     buffer.data);
	    },
	},
	image.data);

    // if any of the attempts to load the data failed, we havent written the image
    // so handle is null
    if (newImage.image == VK_NULL_HANDLE) {
	return {};
    } else {
	//다 만든 이미지 반납
	return newImage;
    }
}

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(VulkanEngine* engine,std::string_view filePath)
{
    //에러 관련 혹은 노드 생성에 집접적으로 과현 하지 않는 데이타
    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<AllocatedImage> images;
    std::vector<std::shared_ptr<GLTFMaterial>> materials;

    //실제 가지고 오기
    fmt::print("Loading GLTF: {}", filePath);
    //LoadGLTF라는 struct를 일단 함수가 끝나도 계속 남개 하고 싶어
    //스마트 포인터로 heap에다가 저장
    std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
    scene->creator = engine;
    //이거는 file->image이러고 싶지 않아서
    //scene.image이런식으로 하고 싶어
    //그래서 *scene.get으로 진짜 LoadGLTF를 가리키게 만드는 거임
    LoadedGLTF& file = *scene.get();
    fastgltf::Parser parser {};

    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;
    //이제 여기서부터는 GLTF가지고 오기 프로젝트임
    fastgltf::GltfDataBuffer data;
    data.loadFromFile(filePath);
    fastgltf::Asset gltf;
    std::filesystem::path path = filePath;
    auto type = fastgltf::determineGltfFileType(&data);
    if (type == fastgltf::GltfType::glTF) {
        auto load = parser.loadGltf(&data, path.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        } else {
            std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
            return {};
        }
    } else if (type == fastgltf::GltfType::GLB) {
        auto load = parser.loadGltfBinary(&data, path.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        } else {
            std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
            return {};
        }
    } else {
        std::cerr << "Failed to determine glTF container" << std::endl;
        return {};
    }

    //이제 디스크립터 pool(사이즈만 정의)
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = { 
	{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
	{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
	{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }
    };

    //현제는 material(재질) 개수만큼 사이즈를 늘려주라 이 말이다
    file.descriptorPool.init(engine->_device, gltf.materials.size(), sizes);

    //여기는 이제 fastgltf로 smapler및 mipmap 적용
    for (fastgltf::Sampler& sampler : gltf.samplers) {

        VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr};
        sampl.maxLod = VK_LOD_CLAMP_NONE;
        sampl.minLod = 0;

        sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
        sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        sampl.mipmapMode= extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        VkSampler newSampler;
        vkCreateSampler(engine->_device, &sampl, nullptr, &newSampler);

        file.samplers.push_back(newSampler);
    }

    //여기서 이제 모든 texture 파일들을 가지고 온다
    for (fastgltf::Image& image : gltf.images) {
	std::optional<AllocatedImage> img = load_image(engine, gltf, image);

	if (img.has_value()) {
	    images.push_back(*img);
	    file.images[image.name.c_str()] = *img;
	    fmt::print("image load succeful (delete me please)");
	}
	else {
	    images.push_back(engine->_errorCheckerboardImage);
	    std::cout << "gltf failed to load texture " << image.name << std::endl;
	}
    }

    // create buffer to hold the material data
    file.materialDataBuffer = engine->create_buffer(sizeof(GLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    //나중에 for loop으로 돌릴거긴 한데 여기는 각 노드마다 material들이 잘들어갔는지(숫자)
    int data_index = 0;
    //CPU --> GPU 포인터 가지고 오기(보내는 준비)
    GLTFMetallic_Roughness::MaterialConstants* sceneMaterialConstants = (GLTFMetallic_Roughness::MaterialConstants*)file.materialDataBuffer.info.pMappedData;

    //이 for loop이 길어보여도 솔직히 간단하다
    //그냥 vk_engine에 있는 GLTFMetallic_Roughness를 다 채우는 것이다
    for (fastgltf::Material& mat : gltf.materials) {
	//아까 위에서 한거하고 비슷하지만 GLTFMaterial이라는 struct에 smartpointer를 만들어서 heap에 계속 살겠금 만드삼
	//GLTFMaterial 안에는 밑에 데이터들을 저장하는 거임
	//MaterialPipeline* pipeline;
	//VkDescriptorSet materialSet;

	std::shared_ptr<GLTFMaterial> newMat = std::make_shared<GLTFMaterial>();
	materials.push_back(newMat);
	file.materials[mat.name.c_str()] = newMat;

	//vk_engine에 가면 이 struct가 존재하는데 다 넣으삼
	GLTFMetallic_Roughness::MaterialConstants constants;
	constants.colorFactors.x = mat.pbrData.baseColorFactor[0];
	constants.colorFactors.y = mat.pbrData.baseColorFactor[1];
	constants.colorFactors.z = mat.pbrData.baseColorFactor[2];
	constants.colorFactors.w = mat.pbrData.baseColorFactor[3];

	constants.metal_rough_factors.x = mat.pbrData.metallicFactor;
	constants.metal_rough_factors.y = mat.pbrData.roughnessFactor;
	//방금 만든 포인터에다가 이제 모든 데이터를 넣으삼
	//이러므로써 이제 각각 마테리얼에 데이타가 저장 된거임
	sceneMaterialConstants[data_index] = constants;

	//blending 관련이 된것들(vk_type가서 보삼)
	MaterialPass passType = MaterialPass::MainColor;
	if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
	    passType = MaterialPass::Transparent;
	}

	//이것도 반복 근데 material color 및 샘플링 관련
	GLTFMetallic_Roughness::MaterialResources materialResources;
	// default the material textures
	materialResources.colorImage = engine->_whiteImage;
	materialResources.colorSampler = engine->_defaultSamplerLinear;
	materialResources.metalRoughImage = engine->_whiteImage;
	materialResources.metalRoughSampler = engine->_defaultSamplerLinear;

	// set the uniform buffer for the material data
	materialResources.dataBuffer = file.materialDataBuffer.buffer;
	materialResources.dataBufferOffset = data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);
	// grab textures from gltf file
	if (mat.pbrData.baseColorTexture.has_value()) {
	    size_t img = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
	    size_t sampler = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();

	    materialResources.colorImage = images[img];
	    materialResources.colorSampler = file.samplers[sampler];
	}
	// buffer write로 마테리얼드 만들기
	newMat->data = engine->metalRoughMaterial.write_material(engine->_device, passType, materialResources, file.descriptorPool);

	data_index++;
    }

    //이제 진짜 노드들 메쉬들 가지고 오기(방금은 그냥 정보들만(mesh, material, texture etc)넣어 노은거임)
    //이제 진짜 vertex, indicies 가지고 오는거임
    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;

    for (fastgltf::Mesh& mesh : gltf.meshes) {
	//newmesh라는 변수는 찐 데이터가 공간을 가리키는 화살표를 가진다
	//newmesh를 소환하면서 메모리 공간을 파고 거기를 가리킬수 있는 포인터까지 주워진 샘이다.
	std::shared_ptr<MeshAsset> newmesh = std::make_shared<MeshAsset>();
	//화살표 복사본 meshes(아까 위에서 만든)
	meshes.push_back(newmesh);
	//여기도 file.meshes(원본) 화살표 복사본 넣기
	file.meshes[mesh.name.c_str()] = newmesh;
	newmesh->name = mesh.name;

	// clear the mesh arrays each mesh, we dont want to merge them by error
	indices.clear();
	vertices.clear();

	for (auto&& p : mesh.primitives) {
	    //각각 sumesh들 체우기
	    GeoSurface newSurface;
	    newSurface.startIndex = (uint32_t)indices.size();
	    newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

	    size_t initial_vtx = vertices.size();

	    // load indexes
	    {
		fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
		indices.reserve(indices.size() + indexaccessor.count);

		fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor,
					   [&](std::uint32_t idx) {
					   indices.push_back(idx + initial_vtx);
					   });
	    }

	    // load vertex positions
	    {
		fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
		vertices.resize(vertices.size() + posAccessor.count);

		fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
						[&](glm::vec3 v, size_t index) {
						Vertex newvtx;
						newvtx.position = v;
						newvtx.normal = { 1, 0, 0 };
						newvtx.color = glm::vec4 { 1.f };
						newvtx.uv_x = 0;
						newvtx.uv_y = 0;
						vertices[initial_vtx + index] = newvtx;
						});
	    }

	    // load vertex normals
	    auto normals = p.findAttribute("NORMAL");
	    if (normals != p.attributes.end()) {

		fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second],
						[&](glm::vec3 v, size_t index) {
						vertices[initial_vtx + index].normal = v;
						});
	    }

	    // load UVs
	    auto uv = p.findAttribute("TEXCOORD_0");
	    if (uv != p.attributes.end()) {

		fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second],
						[&](glm::vec2 v, size_t index) {
						vertices[initial_vtx + index].uv_x = v.x;
						vertices[initial_vtx + index].uv_y = v.y;
						});
	    }

	    // load vertex colors
	    auto colors = p.findAttribute("COLOR_0");
	    if (colors != p.attributes.end()) {

		fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second],
						[&](glm::vec4 v, size_t index) {
						vertices[initial_vtx + index].color = v;
						});
	    }

	    if (p.materialIndex.has_value()) {
		newSurface.material = materials[p.materialIndex.value()];
	    } else {
		newSurface.material = materials[0];
	    }
	    //첮번째 정점을 위치로 일단은 새팅
	    glm::vec3 minpos = vertices[initial_vtx].position;
	    glm::vec3 maxpos = vertices[initial_vtx].position;
	    //가장 왼쪽/아래/뒤에 있는 좌표(minpos)와 가장 오른쪽/위/앞에 있는 좌표(maxpos)를 찾아내는 작업
	    for (int i = initial_vtx; i < vertices.size(); i++) {
		minpos = glm::min(minpos, vertices[i].position);
		maxpos = glm::max(maxpos, vertices[i].position);
	    }
	    //물체의 중심
	    newSurface.bounds.origin = (maxpos + minpos) / 2.f;
	    //중심으로 부터 맨 끝까지에 거리
	    newSurface.bounds.extents = (maxpos - minpos) / 2.f;
	    //물체 크기만한 공을 만들어서 공에다가 늠
	    newSurface.bounds.sphereRadius = glm::length(newSurface.bounds.extents);

	    newmesh->surfaces.push_back(newSurface);
	}

	newmesh->meshBuffers = engine->uploadMesh(indices, vertices);
    }
    for (fastgltf::Node& node : gltf.nodes) {
	//vk_type에 가면 이게 어떻게 생겨 먹었는지 나옴
        std::shared_ptr<Node> newNode;

	//만약 노드(fastgltf::Node.meshIndex안에 있는거)에 값이 존재 한다면??
        if (node.meshIndex.has_value()) {
	    //일단 우리 바구니Node를 heap공간에다가 파삼
	    //여기서 newNode의 data tyoe을 Node --> MeshNode로바꿈
            newNode = std::make_shared<MeshNode>();
	    //아까는 그냥 Node였지만 이제는 MeshNode로 변환 및 mesh를 가지고 옴
	    //아까 가지고 온 메쉬들은 이제 다 여기에다가 넣어 놓는다(vertex, index)
            static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
        } else {
            newNode = std::make_shared<Node>();
        }

	//이제 각각 임시 노드(nodes) 원본 노드(file.nodes)가 공간 newNode를 바라보게끔 설정
        nodes.push_back(newNode);
        file.nodes[node.name.c_str()];

	//노드(부속품)가 부모를 기준으로 어디에 배치되어야 하는가"에 대한 '좌표축 명세서'
	//translate, rotate, scale을 이용해서 정확이 이 노드가 부모를 기준으로 위치하는지 최종적인 결정
	std::visit(fastgltf::visitor { [&](fastgltf::Node::TransformMatrix matrix) { memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));},
	[&](fastgltf::TRS transform) {
	    glm::vec3 tl(transform.translation[0], transform.translation[1],
	   transform.translation[2]);
	    glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1],
	    transform.rotation[2]);
	    glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

	    glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
	    glm::mat4 rm = glm::toMat4(rot);
	    glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

	    newNode->localTransform = tm * rm * sm;
	} },
	node.transform);
    }
    for (int i = 0; i < gltf.nodes.size(); i++) {
	fastgltf::Node& node = gltf.nodes[i];
	//비슷하게 위에서 만들어진 노드들을 다 넣어둔다
	std::shared_ptr<Node>& sceneNode = nodes[i];

	//부모를 와 자식을 연결을 한다
	for (auto& c : node.children) {
	    //각각 포인터 삽입(자식 모록에 추가를 해준다)
	    sceneNode->children.push_back(nodes[c]);
	    //위에 언급한 nodes에다가 삽입(자식의 정보를 주면서 부모한테)--> 자식이 부모한테 정보를 준다
	    nodes[c]->parent = sceneNode;
	}
    }

    // find the top nodes, with no parents
    for (auto& node : nodes) {
	//만약 부모가 없으면?? 너는 최상위 부모로 간주한다
	if (node->parent.lock() == nullptr) {
	    //만약 찾았다면 최상위 부모 노드를 file.topNode에다가 삽입
	    file.topNodes.push_back(node);
	    //reset
	    node->refreshTransform(glm::mat4 { 1.f });
	}
    }
    //완성이 된 scene을 반납
    return scene;
}
void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
    //topNode에 struct는 평범한 <Node>다
    //하지만 vk_loader를 보면 virtual이라고 적혀 있음
    //그래서 이거 이후는 MeshNode에 있는 Draw를 실행을 한다.
    for (auto& n : topNodes) {
        n->Draw(topMatrix, ctx);
    }
}
void LoadedGLTF::clearAll()
{
    VkDevice dv = creator->_device;

    descriptorPool.destroy_pools(dv);
    creator->destroy_buffer(materialDataBuffer);

    for (auto& [k, v] : meshes) {

		creator->destroy_buffer(v->meshBuffers.indexBuffer);
		creator->destroy_buffer(v->meshBuffers.vertexBuffer);
    }

    for (auto& [k, v] : images) {
        
        if (v.image == creator->_errorCheckerboardImage.image) {
            //dont destroy the default images
            continue;
        }
        creator->destroy_image(v);
    }

	for (auto& sampler : samplers) {
		vkDestroySampler(dv, sampler, nullptr);
    }
}
