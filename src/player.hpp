#pragma once

#include "input.hpp"
#include "scene.hpp"
#include <glm/glm.hpp>

// playercommand is temp, so not here
// player state is survives between frames
struct Player
{
    // camera position in the world
    glm::vec3 eyePosition {0.0f, 1.8f, 0.0f};
    // angle left/right around world's vertical Y axis
    // -90 should be facing negative Z
    float yawDegrees = -90.0f;
    // angle up/down
    // 0 should be horizontal
    float pitchDegrees = 0.0f;

    // overwrite camera location & angles from the scene file.
    void teleport(const PlayerSpawn& spawn);
    // modify position & orientation according to input
    void update(const PlayerCommand& command, float deltaSeconds);
    // calculate a unit vector pointing where the player is looking
    glm::vec3 forward() const;
    // calculate camera matrix to give to opengl
    glm::mat4 viewMatrix() const;
};
