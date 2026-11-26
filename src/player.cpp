#include "player.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

glm::vec3 asVec3(const Position& p)
{
    return { p.x, p.y, p.z };
}

glm::vec3 forward(const Orientation& o)
{
    const float yaw = glm::radians(o.yaw), pitch = glm::radians(o.pitch);

    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
}

glm::mat4 viewMatrix(const CameraPose& pose)
{
    const glm::vec3 eye = asVec3(pose.position);

    return glm::lookAt(eye, eye + forward(pose.orientation), glm::vec3(0,1,0));
}

void updatePlayer(Position& position, Orientation& orientation, const PlayerCommand& playerCommand, float deltaTime)
{
    orientation.yaw += playerCommand.lookYaw;
    orientation.pitch = std::clamp(orientation.pitch + playerCommand.lookPitch, -89.0f, 89.0f);

    const glm::vec3 frwrd = forward(orientation);
    const glm::vec3 right = glm::normalize(glm::cross(frwrd, glm::vec3(0,1,0)));

    glm::vec3 movement = frwrd * playerCommand.forward + right * playerCommand.right + glm::vec3(0,1,0) * playerCommand.up;

    if (glm::dot(movement,movement) > 1.0f)
    {
        movement = glm::normalize(movement);
    }

    auto next = asVec3(position) + movement * (playerCommand.sprint ? 12.0f : 5.0f) * deltaTime;

    position = { next.x, next.y, next.z };
}
