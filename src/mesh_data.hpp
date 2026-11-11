#pragma once

#include <cstdint>
#include <vector>

// Output type for both obj loaders. Texture UVs use obj's bottom-left origin.
// p - position
// n - normals (direction)
// uv - texture uv
struct Vertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};
