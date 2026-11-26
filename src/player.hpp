#pragma once

#include "components.hpp"
#include "input.hpp"
#include <glm/glm.hpp>

// CameraPose is a read-only snapshot for rendering (not the persistent player state)
struct CameraPose
{
    Position position;
    Orientation orientation;
};

glm::vec3 asVec3(const Position& position);

glm::vec3 forward(const Orientation& orientation);

glm::mat4 viewMatrix(const CameraPose& pose);

void updatePlayer(Position& position, Orientation& orientation, const PlayerCommand& command, float dt);
