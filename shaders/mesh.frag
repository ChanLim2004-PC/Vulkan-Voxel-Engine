#version 450

#extension GL_GOOGLE_include_directive : require
#include "./input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 0) out vec4 outFragColor;

void main() {
    //그냥 sunlight 생성
    float lightValue = max(dot(inNormal, sceneData.sunlightDirection.xyz), 0.1f);

    vec3 color = inColor * texture(colorTex,inUV).xyz;

    //일단은 ambient만 적용하기
    vec3 ambient = color *  sceneData.ambientColor.xyz;
    //specular까지 적용
    vec4 specular = vec4(color * lightValue *  sceneData.sunlightColor.w + ambient ,1.0f); 

    outFragColor = specular;
}
