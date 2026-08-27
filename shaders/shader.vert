#version 450
//버퍼들을 다룰때 쓰는 extension
#extension GL_EXT_buffer_reference : require

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec3 outPosition;

//uniform Buffer for our 
layout(set = 1, binding = 3) uniform VoxelModel{
    mat4 singleTileModel;
} voxelModel;

layout(std140, set = 3, binding = 0) uniform SceneDataBuffer {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPosition;
    vec4 sunDirection;   
    vec4 sunColor;    
}sceneDataBuffer;


//set4 --> generalUniform

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
}; 

//그냥 vertcies를 담을 변수들이다 --> ssbo는 std430 / ubo는 std140
layout(buffer_reference, std430) readonly buffer VertexBuffer{ 
    Vertex vertices[];
};

//push constants block 이제 여기서 메트릭스 그리고 vertexBuffer주소를 가지고 오는거임
layout( push_constant ) uniform constants
{	
    mat4 render_matrix;
    VertexBuffer vertexBuffer;
} PushConstants;

void main() 
{	
    //load vertex data from device adress
    //버택스 데이터를 전부다 가지고 옴
    Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
    vec4 worldPosition = voxelModel.singleTileModel * vec4(v.position, 1.0f);

    //이제 맞게 넣어준다
    gl_Position = PushConstants.render_matrix * voxelModel.singleTileModel * vec4(v.position, 1.0f);
    //이것은 나중에 너가 지우삼
    gl_Position = sceneDataBuffer.viewProj * worldPosition;
    outColor = v.color.xyz;
    outPosition = worldPosition.xyz;
}
