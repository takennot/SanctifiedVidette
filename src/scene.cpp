#include "scene.hpp"

#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace
{
    // remove surrounding spaces, tabs, carriage returns (\r) & newlines (\n) from a string
    std::string trim(std::string text)
    {
        // find first character that is not whitespace
        const auto first = text.find_first_not_of(" \t\r\n");

        // if not found, return empty
        if (first == std::string::npos)
        {
            return {};
        }

        // find last useful charcter
        const auto last = text.find_last_not_of(" \t\r\n");

        // keep only part between first and last
        return text.substr(first, last - first + 1);
    }

    // Convert one numeric token strictly: "12xyz", NaN and infinity are invalid.
    // basically same idea as obj number parser
    // extract one word from the line
    // convert it to a float
    // reject empty/non-numeric text, garbage after a number, out of range results, infinity & NaN
    // store the number in the caller's "out" reference on success
    bool readNumber(std::istringstream& stream, float& out)
    {
        std::string token;

        if (!(stream >> token))
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const float value = std::strtof(token.c_str(), &end);

        if (end == token.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(value))
        {
            return false;
        }

        out = value;
        return true;
    }

    // after reading the "expected" fields, check that nothing unexpected is left
    // if there is no more text, we good
    // if the next word begins with "#", treat it as inline comment, aka we good
    // otherwise reject extra value(s)
    bool finishLine(std::istringstream& stream)
    {
        std::string extra;
        // Inline # comments are allowed, but unexplained extra arguments are errors.
        return !(stream >> extra) || (!extra.empty() && extra[0] == '#');
    }
}

SceneLoadResult parseScene(std::string_view text)
{
    SceneLoadResult result;
    // for getting line by line
    std::istringstream input(std::string{text});
    std::string line;
    std::size_t lineNumber = 0;
    std::optional<EntityData> current;
    std::unordered_set<std::string> names, fields;
    // prevents duplicates & checks if all reqs are done
    bool seenPlayers[2] = {false, false};
    int goals = 0, objects = 0;

    auto fail = [&](const std::string& why)
    {
        result.error = "Line " + std::to_string(lineNumber) + ": " + why;
        return result;
    };

    // for each line
    while (std::getline(input, line))
    {
        ++lineNumber;
        line = line.substr(0, line.find('#'));
        // read word by word
        std::istringstream stream(line);
        std::string type;

        // ignore empty lines or comment lines
        if (!(stream >> type))
        {
            continue;
        }

        bool valid = true;

        if (!current)
        {
            if (type == "scene" && result.scene.name.empty())
            {
                std::getline(stream, line);
                result.scene.name = trim(line);

                if (result.scene.name.empty())
                {
                    return fail("Missing scene name");
                }
            }
            else if (type == "entity" && !result.scene.name.empty())
            {
                std::getline(stream, line);
                line = trim(line);

                if (line.empty() || !names.insert(line).second)
                {
                    return fail("Missing or duplicate entity name");
                }

                current.emplace();
                current->name.value = line;
                fields.clear();
            }
            else
            {
                return fail("Expected scene name or entity name");
            }

            continue;
        }

        auto& e = *current;

        if (type == "end")
        {
            if (!finishLine(stream) || !fields.contains("position"))
            {
                return fail("Entity needs position and an end (no arguments after end)");
            }

            // might change in the future actually (like playing tag gamemode)
            if (e.player && e.goal)
            {
                return fail("An entity cannot be both player and goal");
            }

            if (e.player || e.goal)
            {
                if (e.mesh || e.texture || e.rotation || fields.contains("scale"))
                {
                    return fail("Players/goals use built-in visuals; mesh, texture, scale and rotation are for objects");
                }

                if (e.player)
                {
                    if (!fields.contains("orientation"))
                    {
                        return fail("Player needs orientation");
                    }

                    if (seenPlayers[e.player->index])
                    {
                        return fail("Duplicate player index");
                    }

                    seenPlayers[e.player->index] = true;
                }
                else
                {
                    if (fields.contains("orientation"))
                    {
                        return fail("Goal uses a fixed orientation");
                    }

                    // Might change in the future (multiple goals, like each player has own goal OR one goal after another OR idk)
                    if (++goals > 1)
                    {
                        return fail("Duplicate goal");
                    }
                }
            }
            else
            {
                if (!e.mesh || !e.texture)
                {
                    return fail("Object needs mesh and texture (default or none allowed)");
                }

                // probably change in hte future
                if (e.orientation.pitch != 0)
                {
                    return fail("Objects currently rotate around Y only; pitch must be 0");
                }

                ++objects;
            }
            result.scene.entities.push_back(std::move(e));
            current.reset();
            continue;
        }

        if (!fields.insert(type).second)
        {
            return fail("Duplicate component: " + type);
        }

        bool ok = true;
        if (type == "position")
        {
            ok = readNumber(stream, e.position.x) && readNumber(stream, e.position.y) && readNumber(stream, e.position.z);
        }
        else if (type == "orientation")
        {
            ok = readNumber(stream,e.orientation.yaw) && readNumber(stream,e.orientation.pitch) && std::abs(e.orientation.pitch) <= 89;
        }
        else if (type == "scale")
        {
            ok = readNumber(stream,e.scale.value) && e.scale.value > 0;
        }
        else if (type == "mesh")
        {
            e.mesh.emplace();
            ok = bool(stream >> e.mesh->model);
        }
        else if (type == "texture")
        {
            e.texture.emplace();
            ok = bool(stream >> e.texture->path);
        }
        else if (type == "rotation")
        {
            e.rotation.emplace();
            ok = readNumber(stream,e.rotation->degreesPerSecond);
        }
        else if (type == "player")
        {
            std::string id;
            ok = bool(stream >> id) && (id == "1" || id == "2");
            if (ok)
            {
                e.player = Player{id == "1" ? 0 : 1};
            }
        }
        else if (type == "goal")
        {
            e.goal = true;
        }
        else
        {
            return fail("Unknown component: " + type);
        }

        if (!ok || !finishLine(stream))
        {
            return fail("Invalid component: " + type);
        }
    }

    if (current)
    {
        return fail("Missing end for entity " + current->name.value);
    }

    if (result.scene.name.empty() || !seenPlayers[0] || !seenPlayers[1] || goals != 1 || objects < 2)
    {
        return fail("Scene needs a name, both players, one goal and at least two objects");
    }

    return result;
}

SceneLoadResult loadScene(const std::string& path)
{
    // try open file at path
    std::ifstream file(path);

    // return error if fail
    if (!file)
    {
        SceneLoadResult result;
        result.error = "Cannot open scene file: " + path;
        return result;
    }

    // read whole file into contents
    std::ostringstream contents;
    contents << file.rdbuf();
    // structure the data
    SceneLoadResult result = parseScene(contents.str());

    // if parsing fails, prefix error with file path so we know which scenes did that
    if (!result.ok())
    {
        result.error = path + ": " + result.error;
    }

    return result;
}

bool SceneCollection::load(const std::vector<std::string>& paths, std::string& error)
{
    std::vector<SceneData> next;
    std::unordered_set<std::string> names;

    // for each provided path, call loadScene
    for (const auto& path : paths)
    {
        auto result = loadScene(path);

        // if file fails, store the error, clear loaded scenes & return false
        if (!result.ok())
        {
            error = result.error;
            return false;
        }

        // check & reject duplicates
        if (!names.insert(result.scene.name).second)
        {
            error = "Duplicate scene name: " + result.scene.name;
            return false;
        }

        // if scene is okay, move it to scenes_
        next.push_back(std::move(result.scene));
    }

    // at least 3 scenes innit
    if (next.size() < 3)
    {
        error = "At least three scene files are required";
        return false;
    }

    // loaded all successfully
    scenes_ = std::move(next);
    error.clear();
    return true;
}
