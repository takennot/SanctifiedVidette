#include "ecs_world.hpp"
#include <cmath>

GameWorld::GameWorld() :
    players_(world_.query_builder<Position, Orientation, const Player>().cached().build()),
    cameras_(world_.query_builder<const Position, const Orientation, const Player>().cached().build()),
    rotating_(world_.query_builder<Orientation, const Rotation>().cached().build()),
    renderables_(world_.query_builder<const Position, const Orientation, const Scale, const Mesh, const Texture>().cached().build()),
    goals_(world_.query_builder<const Position>().with<Goal>().cached().build()),
    names_(world_.query_builder<const Name>().cached().build()) {}

void GameWorld::activate(const SceneData& scene)
{
    // Destruction happens outside query iteration. Cached queries survive scene changes.
    for (auto entity : entities_)
    {
        entity.destruct();
    }

    entities_.clear();
    entities_.reserve(scene.entities.size());

    for (const auto& data : scene.entities)
    {
        auto e = world_.entity();
        e.set<Name>(data.name).set<Position>(data.position);

        if (data.player)
        {
            e.set<Player>(*data.player).set<Orientation>(data.orientation);
        }

        if (data.goal)
        {
            e.add<Goal>();
        }

        if (data.mesh)
        {
            e.set<Mesh>(*data.mesh).set<Texture>(*data.texture).set<Orientation>(data.orientation).set<Scale>(data.scale);
        }

        if (data.rotation)
        {
            e.set<Rotation>(*data.rotation);
        }

        entities_.push_back(e);
    }
}

void GameWorld::resetPlayers(const SceneData& scene)
{
    // Reset each player to their scene spawn.
    players_.each([&](Position& position, Orientation& orientation, const Player& player)
    {
        for (const auto& spawn : scene.entities)
        {
            // Skip entities that aren't players.
            if (!spawn.player)
            {
                continue;
            }

            // Skip entities belonging to a different player.
            if (spawn.player->index != player.index)
            {
                continue;
            }

            position = spawn.position;
            orientation = spawn.orientation;

            break;
        }
    });
}

void GameWorld::update(const FrameCommands& commands, bool multiplayer, float dt, float animationFactor)
{
    // Player movement system. Input is device-independent; P2 sleeps in solo mode.
    // Update each player using their input commands.
    players_.each([&](Position& position, Orientation& orientation, const Player& player)
    {
        // Do not update Player 2 in single-player mode.
        if (player.index == 1 && !multiplayer)
        {
            return;
        }

        const PlayerCommand& command = commands.player[player.index];

        updatePlayer(position, orientation, command, dt);
    });

    // Rotation system. Runs ONCE per frame, never per viewport or shadow pass.
    // Update the rotation of all rotating entities.
    if (animationFactor != 0.0f)
    {
        rotating_.each([&](Orientation& orientation, const Rotation& rotation)
        {
            float rotationAmount = rotation.degreesPerSecond * dt * animationFactor;

            orientation.yaw += rotationAmount;
            orientation.yaw = std::fmod(orientation.yaw, 360.0f);
        });
    }
}

CameraPose GameWorld::camera(int index) const
{
    CameraPose pose{};
    bool found = false;
    // Find the requested player's camera.
    cameras_.each([&](const Position& position, const Orientation& orientation, const Player& player)
    {
        if (player.index != index)
        {
            return;
        }

        pose.position = position;
        pose.orientation = orientation;

        found = true;
    });

    if (!found)
    {
        throw std::logic_error("Missing ECS player");
    }

    return pose;
}

Position GameWorld::goalPosition() const
{
    Position position{};
    bool found = false;
    goals_.each([&](const Position& p)
    {
        position = p;
        found = true;
    });

    if (!found)
    {
        throw std::logic_error("Missing ECS goal");
    }

    return position;
}
