#include "scene.hpp"

#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <fstream>
#include <sstream>
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

    // load 3 floats to xyz position
    // stops if one of the components is invalid
    bool position(std::istringstream& s, Vec3Data& p)
    {
        return readNumber(s, p.x) && readNumber(s, p.y) && readNumber(s, p.z);
    }
}

SceneLoadResult parseScene(std::string_view text)
{
    SceneLoadResult result;
    // for getting line by line
    std::istringstream input(std::string{text});
    std::string line;
    std::size_t lineNumber = 0;
    // prevents duplicates & checks if all reqs are done
    bool seenName = false, seenP1 = false, seenP2 = false, seenGoal = false;

    // for each line
    while (std::getline(input, line))
    {
        ++lineNumber;
        // read word by word
        std::istringstream stream(line);
        std::string type;

        // ignore empty lines or comment lines
        if (!(stream >> type) || type[0] == '#')
        {
            continue;
        }

        bool valid = true;

        if (type == "scene")
        {
            // if already seen scene, mark invalid/duplicate
            if (seenName)
            {
                valid = false;
            }

            // read title, trim whitespaces
            std::string name;
            std::getline(stream, name);
            result.scene.name = trim(name);
            // A scene title can contain spaces; "#" starts an optional comment.
            const auto hash = result.scene.name.find('#');

            // if there is a comment, remove it + everything after it
            if (hash != std::string::npos)
            {
                result.scene.name = trim(result.scene.name.substr(0, hash));
            }

            // reject empty name
            valid = valid && !result.scene.name.empty();
            // mark name as seen
            seenName = true;
        }
        else if (type == "player1" || type == "player2")
        {
            // which PLayerSpawn to fill
            const bool isP1 = type == "player1";
            PlayerSpawn& p = isP1 ? result.scene.player1 : result.scene.player2;

            // rejects if seen either player
            if (isP1 ? seenP1 : seenP2)
            {
                valid = false;
            }

            // read xyzyawpitch, check no extra args in line, reject pitch outside range
            valid = valid && position(stream, p.position) && readNumber(stream, p.yawDegrees)
                 && readNumber(stream, p.pitchDegrees) && finishLine(stream)
                 && p.pitchDegrees >= -89.0f && p.pitchDegrees <= 89.0f;

            //mark seen
            if (isP1)
            {
                seenP1 = true;
            }
            else
            {
                seenP2 = true;
            }
        }
        else if (type == "goal")
        {
            // 1 goal with xyz + comment for validity
            valid = !seenGoal && position(stream, result.scene.goal) && finishLine(stream);
            seenGoal = true;
        }
        else if (type == "object")
        {
            // temp scene object
            SceneObject item;

            // stream >> item.model reads name of cached model
            // cast to bool to get true or false
            // reject a model token starting with "#"
            // read all that
            // check if line is clear after
            // check if scale is positiuve
            valid = static_cast<bool>(stream >> item.model) && item.model[0] != '#'
                 && position(stream, item.position)
                 && readNumber(stream, item.scale)
                 && readNumber(stream, item.yawDegrees)
                 && readNumber(stream, item.rotationSpeed)
                 && finishLine(stream)
                 && item.scale > 0.0f;

            // if valid, move this temp object to scene vector
            if (valid)
            {
                result.scene.objects.push_back(std::move(item));
            }
        }
        else
        {
            valid = false;
        }

        // if anything is wrong, it will be errored, instead of ignored
        // show which keyword & which line caused error, then stop parsing
        if (!valid)
        {
            result.error = "Invalid or duplicate '" + type + "' on scene line " + std::to_string(lineNumber);
            return result;
        }
    }

    // check if scene has everything
    if (!seenName || !seenP1 || !seenP2 || !seenGoal || result.scene.objects.size() < 2)
    {
        result.error = "Scene requires: scene name, player1, player2, goal, and at least two objects";
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
        SceneLoadResult r;
        r.error = "Cannot open scene file: " + path;
        return r;
    }

    // read whole file into contents
    std::ostringstream contents;
    contents << file.rdbuf();
    // structure the data
    SceneLoadResult r = parseScene(contents.str());

    // if parsing fails, prefix error with file path so we know which scenes did that
    if (!r.ok())
    {
        r.error = path + ": " + r.error;
    }

    return r;
}

bool SceneCollection::load(const std::vector<std::string>& paths, std::string& error)
{
    // erase old scenes
    scenes_.clear();

    // for each provided path, call loadScene
    for (const std::string& path : paths)
    {
        auto result = loadScene(path);

        // if file fails, store the error, clear loaded scenes & return false
        if (!result.ok())
        {
            error = result.error;
            scenes_.clear();
            return false;
        }

        // for each scene so far
        for (const auto& previous : scenes_)
        {
            // check & reject duplicates
            if (previous.name == result.scene.name)
            {
                error = "Duplicate scene name: " + result.scene.name;
                scenes_.clear();
                return false;
            }
        }

        // if scene is okay, move it to scenes_
        scenes_.push_back(std::move(result.scene));
    }

    // at least 3 scenes innit
    if (scenes_.size() < 3)
    {
        error = "At least three scene files are required";
        return false;
    }

    // loaded all successfully
    return true;
}
