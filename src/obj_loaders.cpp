#include "obj_loaders.hpp"
#include "mesh_data.hpp"

#include <tiny_obj_loader.h>

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    struct Corner
    {
        int p = -1, uv = -1, n = -1;
        bool operator==(const Corner& other) const
        {
            return p == other.p && uv == other.uv && n == other.n;
        }
    };

    // hash the obj corner, for checking if corner already exists
    struct CornerHash
    {
        std::size_t operator()(const Corner& c) const
        {
            std::size_t h = std::hash<int>{}(c.p);
            // combine hash using XOR
            // 0x9e3779b9u helps with even distribution (reducing collisions)
            // https://www.boost.org/doc/libs/latest/libs/container_hash/doc/html/hash.html#notes_hash_combine
            h ^= std::hash<int>{}(c.uv) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(c.n) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };

    bool parseFloat(const std::string& text, float& value)
    {
        if (text.empty())
        {
            return false;
        }
        char* end = nullptr;
        errno = 0;
        // convert string to float
        value = std::strtof(text.c_str(), &end);
        // success conditions: at least 1 char converted, all chars converted, no out of range error & finite number
        // text.c_str() points to beginning of the string, end points to where conversion stopped
        // if they are equal, conversion never started
        // \0 is end of c string
        return end != text.c_str() &&
               *end == '\0' &&
               errno != ERANGE &&
               std::isfinite(value);
    }

    // example stream input = "1.5 2.0 3.7"
    bool readFloats(std::istringstream& stream, float* values, int count)
    {
        for (int i = 0; i < count; i++)
        {
            std::string token;
            // if we can't read another word from stream or can't convert to float
            // return false
            if (!(stream >> token) || !parseFloat(token, values[i]))
            {
                return false;
            }
        }
        // if can convert all texts to float - success
        return true;
    }

    // convert obj index to cpp index
    bool convertIndex(const std::string& text, std::size_t size, int& out)
    {
        if (text.empty())
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        // 10 = number is decimal aka base 10
        const long long raw = std::strtoll(text.c_str(), &end, 10);

        if (errno != 0 || *end != '\0' || end == text.c_str() || raw == 0)
        {
            return false;
        }

        // if raw is more than 0, just -1 that thing and we good
        // if its negative... then its size + raw
        // obj counts backwards, so -1 is last element and -10 is first for example
        // https://en.wikipedia.org/wiki/Wavefront_.obj_file#Vertex_indices
        // example if size 5, raw -1
        // 5 + -1 = 4 in (0, 1, 2, 3, 4)
        const long long index = raw > 0 ? raw - 1 : static_cast<long long>(size) + raw;

        // check if index is valid
        // cpp has no negative indices so check for that
        // check for indices that are out of range
        // check if index is too large for an int
        if (index < 0 || index >= static_cast<long long>(size) || index > std::numeric_limits<int>::max())
        {
            return false;
        }

        out = static_cast<int>(index);

        return true;
    }

    // token - corner from obj
    // pcount, uvcount, ncount - number of positions/uv coords/normals loaded so far
    // result - corner struct
    bool parseCorner(const std::string& token, std::size_t pCount, std::size_t uvCount, std::size_t nCount, Corner& result)
    {
        // example: if corner is 17/8/9
        // below searches for the first slash (which will be at pos 2 because 1(0) 7(1) /(2))
        const std::size_t first = token.find('/');
        // npos is no position / impossible index int
        // if no slash, then token contains only position index ("f 1 2 3" for example)
        // if its just positions, then we can instantly convert indices
        if (first == std::string::npos)
        {
            return convertIndex(token, pCount, result.p);
        }

        // search for 2nd slash at first+1 offset, so we don't get first slash again
        const std::size_t second = token.find('/', first + 1);
        // if second slash exists within token
        // and third slash exists
        if (second != std::string::npos && token.find('/', second + 1) != std::string::npos)
        {
            // reject if there is 3 slashes because what the hell is it doing there?
            return false;
        }

        // extract position index
        // fail if failed at converting position index
        if (!convertIndex(token.substr(0, first), pCount, result.p))
        {
            return false;
        }

        // extract uv index
        // between first & second slash
        // if second slash does not exist, just take everything until the end
        // if it does - extract "second - first - 1" characters, so number of chars between two slashes
        const std::string uvText = token.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
        // check for empty uv because obj allows it???
        // example 17//9
        if (!uvText.empty() && !convertIndex(uvText, uvCount, result.uv))
        {
            return false;
        }

        // normal index
        // only if 2nd slash exists
        if (second != std::string::npos)
        {
            // extract everything else after 2nd slash
            const std::string nText = token.substr(second + 1);
            // same as uv, if normal exists, convert it. if cant convert, fail.
            // if empty, leave at default value.
            if (!nText.empty() && !convertIndex(nText, nCount, result.n))
            {
                return false;
            }
        }

        return true;
    }

    void warn(ObjLoadResult& out, std::size_t line, const std::string& problem)
    {
        // limit just in case
        if (out.warnings.size() < 20)
        {
            out.warnings.emplace_back("line " + std::to_string(line) + ": " + problem);
        }
    }

    // get triangle normal
    glm::vec3 faceNormal(glm::vec3 a, glm::vec3 b, glm::vec3 c)
    {
        const glm::vec3 cross = glm::cross(b - a, c - a);
        // get squared length
        const float len2 = glm::dot(cross, cross);
        // check if vector length is near zero or invalid
        if (len2 < 1e-14f || !std::isfinite(len2))
        {
            // idk what else to return? just vector up I guess
            return {0.0f, 1.0f, 0.0f};
        }
        // return normalized vector
        return glm::normalize(cross);
    }

    // Deduplicate corners in case if obj face refers to already existing gpu vertex
    // BUT when obj normal is missing, we do NOT share vertex between differently oriented faces
    // to keep flat shading correct
    //
    // append one gpu-ready vertex OR reuse an old one
    // then return its index in mesh.vertices
    //
    // args: vec3 positions, texcoords, normals - obj pos, uv, normals
    // fallbackNormal - calculated from current face, used only if corner has no vn
    // unordered_map - hash map from triple (p, uv, n) to existing gpu vertex index
    std::uint32_t appendCorner(MeshData& mesh, const Corner& c,
                               const std::vector<glm::vec3>& positions,
                               const std::vector<glm::vec2>& texcoords,
                               const std::vector<glm::vec3>& normals,
                               glm::vec3 fallbackNormal,
                               std::unordered_map<Corner, std::uint32_t, CornerHash>& cache)
    {
        // if obj normal exists, reuse cached vertices
        if (c.n >= 0)
        {
            const auto found = cache.find(c);
            if (found != cache.end())
            {
                // return old index if found
                return found->second;
            }
        }

        const glm::vec3 p = positions[static_cast<std::size_t>(c.p)];
        // if no UV index, use (0.0)
        const glm::vec2 uv = c.uv < 0 ? glm::vec2(0.0f) : texcoords[static_cast<std::size_t>(c.uv)];
        // use fallback if no normal index
        const glm::vec3 n = c.n < 0 ? fallbackNormal : normals[static_cast<std::size_t>(c.n)];

        // next free index is current number of vertices
        const auto newIndex = static_cast<std::uint32_t>(mesh.vertices.size());

        // append vertex
        mesh.vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y});

        // if there are provided normals, save for future reuse
        if (c.n >= 0)
        {
            cache.emplace(c, newIndex);
        }
        return newIndex;
    }

    // translate list of 3-4 obj corners into triangle indices for GL_TRIANGLES
    void emitFace(MeshData& mesh, const std::vector<Corner>& face,
                  const std::vector<glm::vec3>& positions,
                  const std::vector<glm::vec2>& uvs,
                  const std::vector<glm::vec3>& normals,
                  std::unordered_map<Corner, std::uint32_t, CornerHash>& cache)
    {
        // fallback normal
        const glm::vec3 normal = faceNormal(positions[face[0].p], positions[face[1].p], positions[face[2].p]);

        // reserve 4 int slots for gpu indices
        std::array<std::uint32_t, 4> corners{};

        for (std::size_t i = 0; i < face.size(); ++i)
        {
            corners[i] = appendCorner(mesh, face[i], positions, uvs, normals, normal, cache);
        }

        // append first triangle
        mesh.indices.insert(mesh.indices.end(), {corners[0], corners[1], corners[2]});

        if (face.size() == 4)
        {
            // append second triangle
            mesh.indices.insert(mesh.indices.end(), {corners[0], corners[2], corners[3]});
        }
    }
}

ObjLoadResult loadCustomObj(const std::string& contents)
{

}

ObjLoadResult loadTinyObj(const std::string& contents)
{

}
