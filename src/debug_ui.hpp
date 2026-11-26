#pragma once

#include "asset_cache.hpp"
#include "input.hpp"
#include "renderer.hpp"
#include "scene.hpp"
#include "player.hpp"
#include <array>
#include <string>

struct GameSettings
{
    RenderOptions render;
    bool gamepadForP2 = true;
    bool animate = true;
    bool reverseAnimation = false;
    float animationSpeed = 1.0f;
};

struct UiActions
{
    int selectScene = -1;
    bool resetPlayers = false;
};

// args:
// settings - game settings so we can change them.
// scenes - get available scene names & active scene details.
// sceneIndex - which scene is currently active.
// assets - show model/mesh cache stats.
// input - ask if controller is connected.
// players - show player to goal distance.
// lastAction - show last keyboard action event
UiActions drawDebugUI(GameSettings& settings, const SceneCollection& scenes,
                      int sceneIndex, const AssetCache& assets, const InputSystem& input,
                      const GameWorld& ecs, const std::string& lastAction);
