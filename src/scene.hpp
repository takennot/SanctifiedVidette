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
// player1 x y z yaw pitch
// player2 x y z yaw pitch
// goal x y z
// object modelName x y z size yawAngle spinSpeed
//
// Goal is a testing thingy, maybe can be reused in the future

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// I could use glm::vec3 instead but then its another GLM dependency
struct Vec3Data
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// starting camera pos + starting look angles
struct PlayerSpawn
{
    Vec3Data position;
    float yawDegrees = -90.0f;
    float pitchDegrees = 0.0f;
};

// model is lookup name in AssetCache, not the path
// yawDegrees is starting angle around world Y
struct SceneObject
{
    std::string model;
    Vec3Data position;
    float scale = 1.0f;
    float yawDegrees = 0.0f;
    // degrees per second
    float rotationSpeed = 0.0f;
};

// level is - a title, 2 player starts, goal & list of objects
struct SceneData
{
    std::string name;
    PlayerSpawn player1;
    PlayerSpawn player2;
    Vec3Data goal;
    std::vector<SceneObject> objects;
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
