#include "obj_loaders.hpp"

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
}
