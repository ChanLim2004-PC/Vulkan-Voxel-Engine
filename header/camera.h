#include "vk_types.h"
#include <SDL3/SDL_events.h>

class Camera {
public:
    float deltaTime;
    glm::vec3 velocity;
    glm::vec3 position;
    glm::mat4 getViewMatrix();
    glm::mat4 getRotationMatrix();
    bool activation = true;
    // vertical rotation
    float pitch { 0.f };
    // horizontal rotation
    float yaw { 0.f };

    void processSDLEvent(SDL_Event& e);

    void ESCKey(SDL_Event& e);

    void update();
};
