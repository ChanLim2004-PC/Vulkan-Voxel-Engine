#include "SDL3/SDL_events.h"
#include "SDL3/SDL_keycode.h"
#include "glm/trigonometric.hpp"
#define GLM_ENABLE_EXPERIMENTAL

#include "../../header/camera.h"
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

void Camera::update()
{
    glm::mat4 cameraRotation = getRotationMatrix();
    float newdeltaTime = deltaTime;
    glm::vec3 moveDir = glm::vec3(cameraRotation * glm::vec4(velocity, 0.0f));
    position += moveDir * (newdeltaTime);
}
void Camera::processSDLEvent(SDL_Event& e)
{
    if (e.type == SDL_EVENT_KEY_DOWN) {
	if (e.key.key == SDLK_W) { velocity.z = -30.0f; } 
	if (e.key.key == SDLK_S) { velocity.z =  30.0f; }
	if (e.key.key == SDLK_A) { velocity.x = -30.0f; }
	if (e.key.key == SDLK_D) { velocity.x =  30.0f; }
    }
    if (e.type == SDL_EVENT_KEY_UP) {
	if (e.key.key == SDLK_W) { velocity.z = 0.f; }
	if (e.key.key == SDLK_S) { velocity.z = 0.f; }
	if (e.key.key == SDLK_A) { velocity.x = 0.f; }
	if (e.key.key == SDLK_D) { velocity.x = 0.f; }
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
	yaw += (float)e.motion.xrel / 200.f;
	pitch -= (float)e.motion.yrel / 200.f;
    }
}
void Camera::ESCKey(SDL_Event& e){
    if (e.type == SDL_EVENT_KEY_DOWN){
	if(Camera::activation == true && e.key.key == SDLK_ESCAPE){
	    Camera::activation = false;
	}
	else
	Camera::activation = true;

    }
}
//View Matrix설정
glm::mat4 Camera::getViewMatrix()
{
    //카메라 움직임
    glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), position);
    //카메라(밑에 언급) 로테이션
    glm::mat4 cameraRotation = getRotationMatrix();
    //그리고 inverse를 시킴
    //카메라가 앞으로 +10만큼 가고 싶었다면 --> 세상을 뒤로 -10만큼 밀어버림.
    return glm::inverse(cameraTranslation * cameraRotation);
}
//로테이션 가지고 오기
glm::mat4 Camera::getRotationMatrix()
{
    //피치를 +x 기준 즉 반시계방향으로 돌리겠다
    glm::degrees(pitch);
    glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3 { 1.f, 0.f, 0.f });
    //clipping the camera(based on radian)
    if(pitch > 1.55f)
        pitch = 1.55f;
    if(pitch < -1.55f)
        pitch = -1.55f;

    //yaw -y 기준 즉 시계방향으로 돌리겠다
    glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3 { 0.f, -1.f, 0.f });
    //이걸 다 matrix로 바꿔서(4*4) 한다
    return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}
