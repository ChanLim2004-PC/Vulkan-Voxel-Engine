layout(set = 0, binding = 0) uniform  SceneData{  
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 ambientColor;
    vec4 sunlightDirection;
    vec4 sunlightColor;
} sceneData;

//방금 만든 유니폼 버퍼(MaterialConstants)
layout(set = 1, binding = 0) uniform GLTFMaterialData{   
    vec4 colorFactors;
    vec4 metal_rough_factors;
} materialData;

//MaterialResources data
layout(set = 1, binding = 1) uniform sampler2D colorTex;
layout(set = 1, binding = 2) uniform sampler2D metalRoughTex;

