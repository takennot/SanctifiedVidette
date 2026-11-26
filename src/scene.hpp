// scene files have .cyno format (cyno brings ships into a system, meanwhile .cyno brings scenes into the engine. Get it? get it?)
// basically a worse version of obj lol
//
// all positions are in world units. Player positions specify eye height.
//
// Angles are in degrees because fuck radians, all my homies hate radians
// https://imgflip.com/i/b6e46x
//
// # is a comment
// scene is a scene title, spaces allowed
// entity Entity Name
// inside you can add components - position, orientation, player, goal, mesh, texture, scale, rotation
// end indicates entity end

#pragma once

#include "components.hpp"
#include <cstddef>
#include <string>
#include <optional>
#include <string_view>
#include <vector>

struct EntityData
{
    Name name;
    Position position;
    Orientation orientation;
    Scale scale;
    std::optional<Mesh> mesh;
    std::optional<Texture> texture;
    std::optional<Player> player;
    std::optional<Rotation> rotation;
    bool goal = false;
};

// level is - a title, 2 player starts, goal & list of objects
struct SceneData
{
    std::string name;
    std::vector<EntityData> entities;
};

// to prevent crashing on parse.
// return both result & errors.
// ok() asks if error message is empty.
// if parsing failed, caller should not use incomplete scene.
struct SceneLoadResult
{
    SceneData scene;
    std::string error;

    bool ok() const
    {
        return error.empty();
    }
};

// interpret already available scene text, does not open a file
// string_view is read only view of string characters. Avoids making a copy when calling function. (we still make copy later lol)
// basically same as obj loader
SceneLoadResult parseScene(std::string_view text);

// read a file, then call parseScene()
SceneLoadResult loadScene(const std::string& path);

class SceneCollection
{
    public:
        // read multiple paths and report whether loading them all worked.
        bool load(const std::vector<std::string>& paths, std::string& error);

        // number of scenes
        std::size_t count() const
        {
            return scenes_.size();
        }

        // read a scene by its index
        // .at() checks bounds and can throw if caller supplies a bad index,
        // so main validates indices before accessing them.
        const SceneData& at(std::size_t index) const
        {
            return scenes_.at(index);
        }

    private:
        // all loaded scene data stored in memory
        std::vector<SceneData> scenes_;
};
