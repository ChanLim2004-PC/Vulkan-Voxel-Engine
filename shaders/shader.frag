#version 460
#extension GL_EXT_ray_query : require

// 가려진 뒷면 픽셀은 광선 연산을 시작도 안 하고 버림
layout(early_fragment_tests) in;

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec3 inPosition;
layout (location = 2) in vec3 inNormal;

layout (location = 0) out vec4 outFragColor;

layout(set = 1, binding = 3) uniform VoxelModel {
    mat4 singleTileModel;
} voxelModel;

layout(set = 2, binding = 0) uniform ScreenData {
    vec4 sceneData;
} screen;

layout(set = 4, binding = 0) uniform accelerationStructureEXT topLevelAS;

float calculateShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
    vec3 rayOrigin = worldPos + (normal); 
    vec3 rayDir = normalize(lightDir);

    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(
        rayQuery, 
        topLevelAS, 
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT, 
        0xFF, 
        rayOrigin, 
        0.01, 
        rayDir, 
        200.0 
    );

    while (rayQueryProceedEXT(rayQuery)) {}

    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
        return 0.25; // 그림자 영역
    }

    return 1.0; // 햇빛 영역
}

void main() 
{
    float region[4] = float[](0.47, 0.50, 0.55, 0.67);

    vec3 groundColor = vec3(0.1, 0.3, 0.1);
    vec3 grassColor  = vec3(0.1, 0.5, 0.1);
    vec3 rockColor   = vec3(0.3, 0.3, 0.3);
    vec3 snowColor   = vec3(1.0, 1.0, 1.0);
    vec3 lightDir    = normalize(vec3(0.4, 0.8, 0.3));

    float persistence = 0.5;
    float amplitude = 300.0f;
    uint octave = 7;
    vec3 blendedColor = groundColor;

    float max_noise_val = amplitude * (1.0f - pow(persistence, float(octave))) / (1.0f - persistence);
    float normalized_noise = (inPosition.y / max_noise_val) * 0.5f + 0.5f;
    float heightFactor = clamp(normalized_noise, 0.0f, 1.0f);

    float t_grass = smoothstep(region[0], region[1], heightFactor);
    blendedColor = mix(blendedColor, grassColor, t_grass);

    float t_rock = smoothstep(region[1], region[2], heightFactor);
    blendedColor = mix(blendedColor, rockColor, t_rock);

    float t_snow = smoothstep(region[2], region[3], heightFactor);
    blendedColor = mix(blendedColor, snowColor, t_snow);

    vec3 N = normalize(inNormal);
    float NdotL = max(dot(N, lightDir), 0.0);

    float shadow = 1.0;
    /*if (NdotL > 0.0) {
        shadow = calculateShadow(inPosition, N, lightDir);
    }*/

    vec3 ambient = vec3(0.4) * blendedColor;
    vec3 diffuse = blendedColor * NdotL * shadow;

    outFragColor = vec4(ambient + diffuse, 1.0);
}
