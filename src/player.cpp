#include "player.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// p is spawn information from current scene file
void Player::teleport(const PlayerSpawn& p)
{
    // copy spawn xyz to camera pos
    eyePosition = {p.position.x, p.position.y, p.position.z};
    // copy yaw
    yawDegrees = p.yawDegrees;
    // clamp pitch between -89 and 89 because it gets confusing when calculating right/forward
    pitchDegrees = std::clamp(p.pitchDegrees, -89.0f, 89.0f);
}

glm::vec3 Player::forward() const
{
    // convert to radians since glm uses radians
    const float yaw = glm::radians(yawDegrees);
    const float pitch = glm::radians(pitchDegrees);

    // x = cos(yaw) * cos(pitch) = horizontal X
    // y = sin(pitch) = looking up makes Y positive
    // z = sin(yaw) * cos(pitch) = horizontal Z
    return glm::normalize(glm::vec3(
                          std::cos(yaw) * std::cos(pitch),
                          std::sin(pitch),
                          std::sin(yaw) * std::cos(pitch)));
}

// creates camera basically
// eyePOsition is camera pos
// target is pos one unit ahead
// up is... up
glm::mat4 Player::viewMatrix() const
{
    return glm::lookAt(eyePosition, eyePosition + forward(), glm::vec3(0, 1, 0));
}

void Player::update(const PlayerCommand& c, float dt)
{
    // add this frame's requested turn amounts to the current camera angles
    // clamp vertical so it don't flip over
    yawDegrees += c.lookYaw;
    pitchDegrees = std::clamp(pitchDegrees + c.lookPitch, -89.0f, 89.0f);

    // f points where camera looks
    const glm::vec3 f = forward();
    // cross product of forward & world up is a direction point right (with these coords at least)
    // normalize to make it consistent
    // this is for strafing
    const glm::vec3 right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));

    // add 3 requested movement directions
    // forward/back along the camera forward vector (including up/down pitch)
    // left/right along camera right
    // vertical ascent/descent along world up, regardless of where camera looks
    glm::vec3 movement = f * c.forward
                       + right * c.right
                       + glm::vec3(0, 1, 0) * c.up;

    // if holding W + D, length of (1, 0, -1) is sqrt(2) so 1.414 ish
    // so diagonal movement is 41% faster
    // dot product with itself gives squared length (2)
    const float length2 = glm::dot(movement, movement);

    // if its more than 1, normalize it back to 1
    if (length2 > 1.0f)
    {
        movement = glm::normalize(movement); // no faster diagonal movement
    }

    // sprint is 12 units per second, otherwise 5
    const float speed = c.sprint ? 12.0f : 5.0f;

    // multiply by delta time to convert speed to distance this frame
    // add that movement to current eye pos
    eyePosition += movement * speed * dt;
}
