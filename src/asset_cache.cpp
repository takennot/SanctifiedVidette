#include "asset_cache.hpp"
#include "obj_loaders.hpp"

#include <iostream>

bool AssetCache::add(const std::string& name, const std::string& objPath, const std::string& texturePath, const std::string& mtlPath)
{
    // if assets_ already contains this name, do nothing, return success
    if (assets_.count(name))
    {
        return true;
    }

    // find or create entry in objText_
    // it - entry (first - path, second - content)
    // inserted - if new entry was created
    auto [it, inserted] = objText_.try_emplace(objPath);

    // if new - read file
    if (inserted)
    {
        // second - obj file content
        it->second = readFile(objPath);
        ++diskReads_; // counted per unique obj path
    }
    // just in case
    if (it->second.empty())
    {
        std::cerr << "Could not open or read OBJ: " << objPath << '\n';
        return false;
    }

    // Both loaders parse the same std::string, so no file reopening
    // these get separate MeshData
    const ObjLoadResult own = loadCustomObj(it->second);

    std::string mtl;

    if (!mtlPath.empty())
    {
        mtl = readFile(mtlPath);
    }

    const ObjLoadResult external = loadTinyObj(it->second, mtl);
    ModelAsset asset;
    asset.path = objPath;

    if (own.ok())
    {
        asset.custom = uploadMesh(own.mesh);
        asset.customValid = asset.custom.vao != 0;
        // 3 indices per triangle so divide by 3 to get index count
        asset.customTriangles = own.mesh.indices.size() / 3;

        if (asset.customValid)
        {
            ++gpuUploads_;
        }
    }
    else
    {
        asset.error += "Custom: " + own.error + "; ";
    }

    if (external.ok())
    {
        asset.external = uploadMesh(external.mesh);
        asset.externalValid = asset.external.vao != 0;
        asset.externalTriangles = external.mesh.indices.size() / 3;
        if (asset.externalValid)
        {
            ++gpuUploads_;
        }
    }
    else
    {
        asset.error += "TinyObj: " + external.error + "; ";
    }

    for (const auto& warning : own.warnings)
    {
        std::cerr << "[Custom " << name << "] " << warning << '\n';
    }

    for (const auto& warning : external.warnings)
    {
        std::cerr << "[TinyObj " << name << "] " << warning << '\n';
    }

    if (!asset.customValid && !asset.externalValid)
    {
        std::cerr << "OBJ failed: " << name << ": " << asset.error << '\n';
        return false;
    }

    // try to reuse a cached texture
    // look up texture path in the hash map
    const auto foundTexture = textures_.find(texturePath);

    // if found reuse existing texture ID
    // if not, load img, remember texture ID
    if (foundTexture != textures_.end())
    {
        asset.texture = foundTexture->second;
    }
    else
    {
        asset.texture = loadTexture(texturePath);
        textures_.emplace(texturePath, asset.texture);
    }

    // insert model asset into the assets_ map under name
    assets_.emplace(name, asset);
    return true;
}

const ModelAsset* AssetCache::find(const std::string& name) const
{
    const auto it = assets_.find(name);
    return it == assets_.end() ? nullptr : &it->second;
}

void AssetCache::clear()
{
    for (auto& pair : assets_)
    {
        destroyMesh(pair.second.custom);
        destroyMesh(pair.second.external);
    }

    for (auto& pair : textures_)
    {
        destroyTexture(pair.second);
    }

    assets_.clear();
    objText_.clear();
    textures_.clear();
    diskReads_ = 0;
    gpuUploads_ = 0;
}
