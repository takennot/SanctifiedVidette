#pragma once

#include "gpu.hpp"
#include <map>
#include <string>
#include <unordered_map>

// represents one model name registered in the scene
// 2 versions + 1 texture
struct ModelAsset
{
    Mesh custom;
    Mesh external;
    gl::GLuint texture = 0;
    std::string path;
    std::string error;
    // for imgui
    std::size_t customTriangles = 0;
    std::size_t externalTriangles = 0;
    bool customValid = false;
    bool externalValid = false;
};

// All meshes and png textures get uploaded once on registration
// this class owns all named models & obj text strings
class AssetCache
{
public:
    // register one model, name is lookup key
    bool add(const std::string& name, const std::string& objPath, const std::string& texturePath, const std::string& mtlPath = "");

    const ModelAsset* find(const std::string& name) const;

    // imgui stuff
    const std::map<std::string, ModelAsset>& items() const
    {
        return assets_;
    }

    // number of registered names (not scene copies and not unique file paths)
    std::size_t uniqueModels() const
    {
        return assets_.size();
    }

    // number of first-seen OBJ file paths read
    int diskReads() const
    {
        return diskReads_;
    }

    // number of OBJ GPU mesh upload attempts counted at registration
    int gpuUploads() const
    {
        return gpuUploads_;
    }

    // release all cache-owned gpu resources while a valid OpenGL context still exists
    void clear();

private:
    // sorted mapping from model name to modelasset
    std::map<std::string, ModelAsset> assets_;
    // hash mapping from obj path to its text / content
    std::unordered_map<std::string, std::string> objText_;
    // same but from obj path to gpu texture
    std::unordered_map<std::string, gl::GLuint> textures_;
    // attempts to read first-seen OBJ paths
    int diskReads_ = 0;
    // successful-parser GPU mesh upload attempts
    int gpuUploads_ = 0;
};
