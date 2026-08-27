#version 450
//shader input
layout (location = 0) in vec3 inColor;
layout (location = 1) in vec3 inPosition;

//output write
layout (location = 0) out vec4 outFragColor;

layout(set = 1, binding = 3) uniform VoxelModel{
    mat4 singleTileModel;
} voxelModel;

layout(set = 2, binding = 0) uniform ScreenData{
    vec4 sceneData;
} screen;

void main() 
{
    float region[4];
    region[0] = 0.47;
    region[1]= 0.50;
    region[2] = 0.55;
    region[3] = 0.67;

    vec3 groundColor = vec3(0.1, 0.3, 0.1); // 초록색 (풀)
    vec3 grassColor = vec3(0.1, 0.5, 0.1); // 초록색 (풀)
    vec3 rockColor  = vec3(0.3, 0.3, 0.3); // 회색 (바위)
    vec3 snowColor  = vec3(1.0, 1.0, 1.0); // 하얀색 (눈)

    float persistence = 0.5;
    float amplitude = 300.0f;
    uint octave = 7;
    vec3 blendedColor = groundColor;

    //이 코드 전체가 그냥 현제 우리y value를 0.0~1.0으로 맞춰주는 코드임
    float max_noise_val = amplitude * (1.0f - pow(persistence, float(octave))) / (1.0f - persistence);
    float normalized_noise = (inPosition.y / max_noise_val) * 0.5f + 0.5f;
    float heightFactor = clamp(normalized_noise, 0.0f, 1.0f);

    float t_grass = smoothstep(region[0], region[1], heightFactor);
    blendedColor = mix(blendedColor, grassColor, t_grass);

    float t_rock = smoothstep(region[1], region[2], heightFactor);
    blendedColor = mix(blendedColor, rockColor, t_rock);

    float t_snow = smoothstep(region[2], region[3], heightFactor);
    blendedColor = mix(blendedColor, snowColor, t_snow);

    outFragColor = vec4(blendedColor, 1.0);
}
