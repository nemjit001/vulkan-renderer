#pragma once

#include "math.hpp"

enum class LightType
{
    Undefined = 0,
    Directional = 1,
    Point = 2,
};

struct Light
{
    LightType type;
    glm::vec3 color;
};
