#pragma once

#include "mesh_data.hpp"
#include <string>
#include <vector>

struct ObjLoadResult
{
    MeshData mesh;
    std::vector<std::string> objectNames; // "o" declarations or one default object
    std::vector<std::string> warnings;
    std::string error;

    bool ok() const { return error.empty() && !mesh.indices.empty(); }
};

// Both functions accept OBJ contents.
// The caller controls i/o and can pass the same bytes to either loader without reopening the file.
ObjLoadResult loadCustomObj(const std::string& contents);
ObjLoadResult loadTinyObj(const std::string& contents);
