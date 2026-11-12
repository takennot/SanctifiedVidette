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
    // btw named this obj vertex a Corner to not confuse with my Vertex struct
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
    ObjLoadResult out;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> normals;
    std::unordered_map<Corner, std::uint32_t, CornerHash> cache;

    // wrap string in istringstream so it behaves like a readable file without reopening the obj file
    std::istringstream input(contents);
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(input, line))
    {
        ++lineNumber;
        // wrap this line in a stream so we can extract fields with >>
        std::istringstream stream(line);
        std::string keyword;

        // if blank line or comment - skip
        if (!(stream >> keyword) || keyword[0] == '#')
        {
            continue;
        }

        // i forgot that switch case in c++ can't handle strings uuugh
        // if object name
        if (keyword == "o")
        {
            std::string name;
            // discard leading spaces, take remaining name incl. spaces
            std::getline(stream >> std::ws, name);

            if (name.empty())
            {
                warn(out, lineNumber, "empty object name");
            }
            else
            {
                // multiple "o" sections share one mesh
                out.objectNames.push_back(name);
            }
        }
        // vertex aka corner aka xyz
        else if (keyword == "v")
        {
            float a[3];

            // if we can read 3 valid floats
            // append the resulting vec3 to positions
            if (readFloats(stream, a, 3))
            {
                positions.emplace_back(a[0], a[1], a[2]);
            }
            else
            {
                // keep running if parsing failed, but keep warning for which line failed
                warn(out, lineNumber, "invalid vertex position");
            }
        }
        // texcoord aka uv
        else if (keyword == "vt")
        {
            float a[2];

            if (readFloats(stream, a, 2))
            {
                texcoords.emplace_back(a[0], a[1]);
            }
            else
            {
                warn(out, lineNumber, "invalid UV coordinates");
            }
        }
        // normal
        else if (keyword == "vn")
        {
            float a[3];

            // almost same as above
            // if can parse floats - normalize
            if (readFloats(stream, a, 3))
            {
                glm::vec3 n(a[0], a[1], a[2]);

                // check if normal is not near zero
                if (glm::dot(n, n) > 1e-12f)
                {
                    normals.push_back(glm::normalize(n));
                }
                else
                {
                    // if vec 0 - just use up vec + warning
                    normals.emplace_back(0.0f, 1.0f, 0.0f);
                    warn(out, lineNumber, "zero normal");
                }
            }
        }
        // face
        else if (keyword == "f")
        {
            std::vector<Corner> face;
            // aka a line, for example 1/2/3 or 1//3
            std::string token;
            bool valid = true;

            while (stream >> token)
            {
                // stop at comment
                if (token[0] == '#')
                {
                    break;
                }

                // invalid if 5th corner appears
                if (face.size() == 4)
                {
                    valid = false;
                    break;
                }
                // p=uv=n=1
                Corner c;

                // check if index is ok
                if (!parseCorner(token, positions.size(), texcoords.size(), normals.size(), c))
                {
                    // if invalid, mark whole face as invalid & exit its token loop.
                    valid = false;
                    break;
                }
                // if valid, append to face list
                face.push_back(c);
            }

            // if invalid, not triangle or not quad
            // then put warning + skip to next obj line
            if (!valid || (face.size() != 3 && face.size() != 4))
            {
                warn(out, lineNumber, "invalid face, missing/out of range index or unsupported ngon");
                continue;
            }
            // for a valid face, acll emitface to make gpu vertices & 3/6 triangle indices
            emitFace(out.mesh, face, positions, texcoords, normals, cache);
        }
    }

    // no name
    if (out.objectNames.empty())
    {
        out.objectNames.push_back("Unnamed OBJ object");
    }

    // if no indices at all, error whole thing
    if (out.mesh.indices.empty())
    {
        out.error = "No valid triangle or quad faces in OBJ data";
    }

    return out;
}

ObjLoadResult loadTinyObj(const std::string& contents)
{
    ObjLoadResult out;
    // apparently the way I used tinyobj before was deprecated api
    // so here is a proper way to do it
    tinyobj::ObjReader reader;
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    // add vertex color support later, but for now disable color parsing
    config.vertex_color = false;

    // add mtl support later
    if (!reader.ParseFromString(contents, "", config))
    {
        // prefix with TinyObjLoader to see which obj loader failed
        out.error = "TinyObjLoader: " + reader.Error();
        if (out.error == "TinyObjLoader: ")
        {
            // print generic error message if tinyobj didn't send any details
            out.error += "could not parse OBJ";
            return out;
        }
    }

    if (!reader.Warning().empty())
    {
        // add warnings if there are any
        out.warnings.push_back(reader.Warning());
    }

    // get all positions, normals, uvs
    const auto& attrib = reader.GetAttrib();
    // get o objects
    const auto& shapes = reader.GetShapes();
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> texcoords;

    // go through positions
    // read in triples (xyz) from attrib.vertices
    // i + 2 < size in case if data is incomplete
    for (std::size_t i = 0; i + 2 < attrib.vertices.size(); i += 3)
    {
        // append one vec3 per 3 input floats
        positions.emplace_back(attrib.vertices[i], attrib.vertices[i + 1], attrib.vertices[i + 2]);
    }

    // read uvs, same as above but pairs (uv)
    for (std::size_t i = 0; i + 1 < attrib.texcoords.size(); i += 2)
    {
        texcoords.emplace_back(attrib.texcoords[i], attrib.texcoords[i + 1]);
    }

    // normals, see above
    for (std::size_t i = 0; i + 2 < attrib.normals.size(); i += 3)
    {
        normals.emplace_back(attrib.normals[i], attrib.normals[i + 1], attrib.normals[i + 2]);
    }

    // same as custom obj loader
    std::unordered_map<Corner, std::uint32_t, CornerHash> cache;

    // go through shapes here
    for (const auto& shape : shapes)
    {
        // get obj name
        out.objectNames.push_back(shape.name.empty() ? "Unnamed OBJ object" : shape.name);
        // for where current face corner starts
        std::size_t offset = 0;

        // go through num of vertices of each face
        for (const unsigned char count : shape.mesh.num_face_vertices)
        {
            // ignore not triangles/quads
            if (count != 3 && count != 4)
            {
                // move offset by corner count
                offset += count;
                continue;
            }

            // check just in case if mesh does not contain "count" amount
            if (offset + count > shape.mesh.indices.size())
            {
                out.error = "TinyObjLoader reported truncated face indices";
                return out;
            }

            // temp list of converted face-corner references
            std::vector<Corner> face;
            bool valid = true;

            // loop through this face's corners
            for (std::size_t j = 0; j < count; ++j)
            {
                // index info for one corner of the current face
                // j - which corner of the face are we looking at
                // offset + j - corner position in the list
                // idx should contain pos, uv & normal indices.
                const auto& idx = shape.mesh.indices[offset + j];
                // copy already read indices to Corner struct
                Corner c{idx.vertex_index, idx.texcoord_index, idx.normal_index};

                // check if pos index is at least 0 & less than pos count
                // and same for uv & normal
                if (c.p < 0 || static_cast<std::size_t>(c.p) >= positions.size() ||
                    (c.uv >= 0 && static_cast<std::size_t>(c.uv) >= texcoords.size()) ||
                    (c.n >= 0 && static_cast<std::size_t>(c.n) >= normals.size()))
                {
                    // if any array entry is out of range - mark as invalid + stop reading its corners
                    valid = false;
                    break;
                }
                // otherwise append corner to this face's list
                face.push_back(c);
            }

            // go to next face
            offset += count;

            // if valid, use emitFace to make MeshData
            if (valid)
            {
                emitFace(out.mesh, face, positions, texcoords, normals, cache);
            }
            else
            {
                // warning if invalid
                out.warnings.push_back("Ignored face with out-of-range indices");
            }
        }
    }

    // if no drawable triangles at this point, just error it idk
    if (out.mesh.indices.empty())
    {
        out.error = "No valid triangle faces returned by TinyObjLoader";
    }

    return out;
}
